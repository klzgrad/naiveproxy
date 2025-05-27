// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_WEBSOCKETS_WEBSOCKET_CHANNEL_H_
#define NET_WEBSOCKETS_WEBSOCKET_CHANNEL_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/queue.h"
#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/i18n/streaming_utf8_validator.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/net_export.h"
#include "net/storage_access_api/status.h"
#include "net/websockets/websocket_event_interface.h"
#include "net/websockets/websocket_frame.h"
#include "net/websockets/websocket_stream.h"
#include "url/gurl.h"

namespace url {
class Origin;
}  // namespace url

namespace net {

class AuthChallengeInfo;
class AuthCredentials;
class HttpRequestHeaders;
class HttpResponseHeaders;
class IOBuffer;
class IPEndPoint;
class IsolationInfo;
class NetLogWithSource;
class SSLInfo;
class SiteForCookies;
class URLRequest;
class URLRequestContext;
struct NetworkTrafficAnnotationTag;
struct WebSocketHandshakeRequestInfo;
struct WebSocketHandshakeResponseInfo;

// Transport-independent implementation of WebSockets. Implements protocol
// semantics that do not depend on the underlying transport. Provides the
// interface to the content layer. Some WebSocket concepts are used here without
// definition; please see the RFC at http://tools.ietf.org/html/rfc6455 for
// clarification.
class NET_EXPORT WebSocketChannel {
 public:
  // The type of a WebSocketStream creator callback. Must match the signature of
  // WebSocketStream::CreateAndConnectStream().
  typedef base::OnceCallback<std::unique_ptr<WebSocketStreamRequest>(
      const GURL&,
      const std::vector<std::string>&,
      const url::Origin&,
      const SiteForCookies&,
      StorageAccessApiStatus,
      const IsolationInfo&,
      const HttpRequestHeaders&,
      URLRequestContext*,
      const NetLogWithSource&,
      NetworkTrafficAnnotationTag,
      std::unique_ptr<WebSocketStream::ConnectDelegate>)>
      WebSocketStreamRequestCreationCallback;

  // Methods which return a value of type ChannelState may delete |this|. If the
  // return value is CHANNEL_DELETED, then the caller must return without making
  // any further access to member variables or methods.
  enum ChannelState { CHANNEL_ALIVE, CHANNEL_DELETED };

  // Creates a new WebSocketChannel in an idle state.
  // SendAddChannelRequest() must be called immediately afterwards to start the
  // connection process.
  WebSocketChannel(std::unique_ptr<WebSocketEventInterface> event_interface,
                   URLRequestContext* url_request_context);

  WebSocketChannel(const WebSocketChannel&) = delete;
  WebSocketChannel& operator=(const WebSocketChannel&) = delete;

  virtual ~WebSocketChannel();

  // Starts the connection process.
  void SendAddChannelRequest(
      const GURL& socket_url,
      const std::vector<std::string>& requested_protocols,
      const url::Origin& origin,
      const SiteForCookies& site_for_cookies,
      StorageAccessApiStatus storage_access_api_status,
      const IsolationInfo& isolation_info,
      const HttpRequestHeaders& additional_headers,
      NetworkTrafficAnnotationTag traffic_annotation);

  // Sends a data frame to the remote side. It is the responsibility of the
  // caller to ensure that they have sufficient send quota to send this data,
  // otherwise the connection will be closed without sending. |fin| indicates
  // the last frame in a message, equivalent to "FIN" as specified in section
  // 5.2 of RFC6455. |buffer->data()| is the "Payload Data". If |op_code| is
  // kOpCodeText, or it is kOpCodeContinuation and the type the message is
  // Text, then |buffer->data()| must be a chunk of a valid UTF-8 message,
  // however there is no requirement for |buffer->data()| to be split on
  // character boundaries. Calling SendFrame may result in synchronous calls to
  // |event_interface_| which may result in this object being deleted. In that
  // case, the return value will be CHANNEL_DELETED.
  [[nodiscard]] ChannelState SendFrame(bool fin,
                                       WebSocketFrameHeader::OpCode op_code,
                                       scoped_refptr<IOBuffer> buffer,
                                       size_t buffer_size);

  // Calls WebSocketStream::ReadFrames() with the appropriate arguments. Stops
  // calling ReadFrames if no writable buffer in dataframe or WebSocketStream
  // starts async read.
  [[nodiscard]] ChannelState ReadFrames();

  // Starts the closing handshake for a client-initiated shutdown of the
  // connection. There is no API to close the connection without a closing
  // handshake, but destroying the WebSocketChannel object while connected will
  // effectively do that. |code| must be in the range 1000-4999. |reason| should
  // be a valid UTF-8 string or empty.
  //
  // Calling this function may result in synchronous calls to |event_interface_|
  // which may result in this object being deleted. In that case, the return
  // value will be CHANNEL_DELETED.
  [[nodiscard]] ChannelState StartClosingHandshake(uint16_t code,
                                                   const std::string& reason);

  // Starts the connection process, using a specified creator callback rather
  // than the default. This is exposed for testing.
  void SendAddChannelRequestForTesting(
      const GURL& socket_url,
      const std::vector<std::string>& requested_protocols,
      const url::Origin& origin,
      const SiteForCookies& site_for_cookies,
      StorageAccessApiStatus storage_access_api_status,
      const IsolationInfo& isolation_info,
      const HttpRequestHeaders& additional_headers,
      NetworkTrafficAnnotationTag traffic_annotation,
      WebSocketStreamRequestCreationCallback callback);

  // The default timout for the closing handshake is a sensible value (see
  // kClosingHandshakeTimeoutSeconds in websocket_channel.cc). However, we can
  // set it to a very small value for testing purposes.
  void SetClosingHandshakeTimeoutForTesting(base::TimeDelta delay);

  // The default timout for the underlying connection close is a sensible value
  // (see kUnderlyingConnectionCloseTimeoutSeconds in websocket_channel.cc).
  // However, we can set it to a very small value for testing purposes.
  void SetUnderlyingConnectionCloseTimeoutForTesting(base::TimeDelta delay);

  // Called when the stream starts the WebSocket Opening Handshake.
  // This method is public for testing.
  void OnStartOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeRequestInfo> request);

 private:
  // The object passes through a linear progression of states from
  // FRESHLY_CONSTRUCTED to CLOSED, except that the SEND_CLOSED and RECV_CLOSED
  // states may be skipped in case of error.
  enum State {
    FRESHLY_CONSTRUCTED,
    CONNECTING,
    CONNECTED,
    SEND_CLOSED,  // A Close frame has been sent but not received.
    RECV_CLOSED,  // Used briefly between receiving a Close frame and sending
                  // the response. Once the response is sent, the state changes
                  // to CLOSED.
    CLOSE_WAIT,   // The Closing Handshake has completed, but the remote server
                  // has not yet closed the connection.
    CLOSED,       // The Closing Handshake has completed and the connection
                  // has been closed; or the connection is failed.
  };

  // Implementation of WebSocketStream::ConnectDelegate for
  // WebSocketChannel. WebSocketChannel does not inherit from
  // WebSocketStream::ConnectDelegate directly to avoid cluttering the public
  // interface with the implementation of those methods, and because the
  // lifetime of a WebSocketChannel is longer than the lifetime of the
  // connection process.
  class ConnectDelegate;

  // Starts the connection process, using the supplied stream request creation
  // callback.
  void SendAddChannelRequestWithSuppliedCallback(
      const GURL& socket_url,
      const std::vector<std::string>& requested_protocols,
      const url::Origin& origin,
      const SiteForCookies& site_for_cookies,
      StorageAccessApiStatus storage_access_api_status,
      const IsolationInfo& isolation_info,
      const HttpRequestHeaders& additional_headers,
      NetworkTrafficAnnotationTag traffic_annotation,
      WebSocketStreamRequestCreationCallback callback);

  // Called when a URLRequest is created for handshaking.
  void OnCreateURLRequest(URLRequest* request);

  // Called when a URLRequest's OnConnected is called. Forwards the call to the
  // |event_interface_|
  void OnURLRequestConnected(URLRequest* request, const TransportInfo& info);

  // Success callback from WebSocketStream::CreateAndConnectStream(). Reports
  // success to the event interface. May delete |this|.
  void OnConnectSuccess(
      std::unique_ptr<WebSocketStream> stream,
      std::unique_ptr<WebSocketHandshakeResponseInfo> response);

  // Failure callback from WebSocketStream::CreateAndConnectStream(). Reports
  // failure to the event interface. May delete |this|.
  void OnConnectFailure(const std::string& message,
                        int net_error,
                        std::optional<int> response_code);

  // SSL certificate error callback from
  // WebSocketStream::CreateAndConnectStream(). Forwards the request to the
  // event interface.
  void OnSSLCertificateError(
      std::unique_ptr<WebSocketEventInterface::SSLErrorCallbacks>
          ssl_error_callbacks,
      int net_error,
      const SSLInfo& ssl_info,
      bool fatal);

  // Authentication request from WebSocketStream::CreateAndConnectStream().
  // Forwards the request to the event interface.
  int OnAuthRequired(const AuthChallengeInfo& auth_info,
                     scoped_refptr<HttpResponseHeaders> response_headers,
                     const IPEndPoint& remote_endpoint,
                     base::OnceCallback<void(const AuthCredentials*)> callback,
                     std::optional<AuthCredentials>* credentials);

  // Sets |state_| to |new_state| and updates UMA if necessary.
  void SetState(State new_state);

  // Returns true if state_ is SEND_CLOSED, CLOSE_WAIT or CLOSED.
  bool InClosingState() const;

  // Calls WebSocketStream::WriteFrames() with the appropriate arguments
  [[nodiscard]] ChannelState WriteFrames();

  // Callback from WebSocketStream::WriteFrames. Sends pending data or adjusts
  // the send quota of the renderer channel as appropriate. |result| is a net
  // error code, usually OK. If |synchronous| is true, then OnWriteDone() is
  // being called from within the WriteFrames() loop and does not need to call
  // WriteFrames() itself.
  [[nodiscard]] ChannelState OnWriteDone(bool synchronous, int result);

  // Callback from WebSocketStream::ReadFrames. Handles any errors and processes
  // the returned chunks appropriately to their type. |result| is a net error
  // code. If |synchronous| is true, then OnReadDone() is being called from
  // within the ReadFrames() loop and does not need to call ReadFrames() itself.
  [[nodiscard]] ChannelState OnReadDone(bool synchronous, int result);

  // Handles a single frame that the object has received enough of to process.
  // May call |event_interface_| methods, send responses to the server, and
  // change the value of |state_|.
  //
  // This method performs sanity checks on the frame that are needed regardless
  // of the current state. Then, calls the HandleFrameByState() method below
  // which performs the appropriate action(s) depending on the current state.
  [[nodiscard]] ChannelState HandleFrame(std::unique_ptr<WebSocketFrame> frame);

  // Handles a single frame depending on the current state. It's used by the
  // HandleFrame() method.
  [[nodiscard]] ChannelState HandleFrameByState(
      const WebSocketFrameHeader::OpCode opcode,
      bool final,
      base::span<const char> payload);

  // Forwards a received data frame to the renderer, if connected. If
  // |expecting_continuation| is not equal to |expecting_to_read_continuation_|,
  // will fail the channel. Also checks the UTF-8 validity of text frames.
  [[nodiscard]] ChannelState HandleDataFrame(
      WebSocketFrameHeader::OpCode opcode,
      bool final,
      base::span<const char> payload);

  // Handles an incoming close frame with |code| and |reason|.
  [[nodiscard]] ChannelState HandleCloseFrame(uint16_t code,
                                              const std::string& reason);

  // Responds to a closing handshake initiated by the server.
  [[nodiscard]] ChannelState RespondToClosingHandshake();

  // Low-level method to send a single frame. Used for both data and control
  // frames. Either sends the frame immediately or buffers it to be scheduled
  // when the current write finishes. |fin| and |op_code| are defined as for
  // SendFrame() above, except that |op_code| may also be a control frame
  // opcode.
  [[nodiscard]] ChannelState SendFrameInternal(
      bool fin,
      WebSocketFrameHeader::OpCode op_code,
      scoped_refptr<IOBuffer> buffer,
      uint64_t buffer_size);

  // Performs the "Fail the WebSocket Connection" operation as defined in
  // RFC6455. A NotifyFailure message is sent to the renderer with |message|.
  // The renderer will log the message to the console but not expose it to
  // Javascript. Javascript will see a Close code of AbnormalClosure (1006) with
  // an empty reason string. If state_ is CONNECTED then a Close message is sent
  // to the remote host containing the supplied |code| and |reason|. If the
  // stream is open, closes it and sets state_ to CLOSED. This function deletes
  // |this|.
  void FailChannel(const std::string& message,
                   uint16_t code,
                   const std::string& reason);

  // Sends a Close frame to Start the WebSocket Closing Handshake, or to respond
  // to a Close frame from the server. As a special case, setting |code| to
  // kWebSocketErrorNoStatusReceived will create a Close frame with no payload;
  // this is symmetric with the behaviour of ParseClose.
  [[nodiscard]] ChannelState SendClose(uint16_t code,
                                       const std::string& reason);

  // Parses a Close frame payload. If no status code is supplied, then |code| is
  // set to 1005 (No status code) with empty |reason|. If the reason text is not
  // valid UTF-8, then |reason| is set to an empty string. If the payload size
  // is 1, or the supplied code is not permitted to be sent over the network,
  // then false is returned and |message| is set to an appropriate console
  // message.
  bool ParseClose(base::span<const char> payload,
                  uint16_t* code,
                  std::string* reason,
                  std::string* message);

  // Drop this channel.
  // If there are pending opening handshake notifications, notify them
  // before dropping. This function deletes |this|.
  void DoDropChannel(bool was_clean, uint16_t code, const std::string& reason);

  // Called if the closing handshake times out. Closes the connection and
  // informs the |event_interface_| if appropriate.
  void CloseTimeout();

  // The URL of the remote server.
  GURL socket_url_;

  // The object receiving events.
  const std::unique_ptr<WebSocketEventInterface> event_interface_;

  // The URLRequestContext to pass to the WebSocketStream creator.
  const raw_ptr<URLRequestContext> url_request_context_;

  // The WebSocketStream on which to send and receive data.
  std::unique_ptr<WebSocketStream> stream_;

  // A data structure containing a vector of frames to be sent and the total
  // number of bytes contained in the vector.
  class SendBuffer;

  // Data that is currently pending write, or NULL if no write is pending.
  std::unique_ptr<SendBuffer> data_being_sent_;
  // Data that is queued up to write after the current write completes.
  // Only non-NULL when such data actually exists.
  std::unique_ptr<SendBuffer> data_to_send_next_;

  // Destination for the current call to WebSocketStream::ReadFrames
  std::vector<std::unique_ptr<WebSocketFrame>> read_frames_;

  // Handle to an in-progress WebSocketStream creation request. Only non-NULL
  // during the connection process.
  std::unique_ptr<WebSocketStreamRequest> stream_request_;

  // Timer for the closing handshake.
  base::OneShotTimer close_timer_;

  // Timeout for the closing handshake.
  base::TimeDelta closing_handshake_timeout_;

  // Timeout for the underlying connection close after completion of closing
  // handshake.
  base::TimeDelta underlying_connection_close_timeout_;

  // Storage for the status code and reason from the time the Close frame
  // arrives until the connection is closed and they are passed to
  // OnDropChannel().
  bool has_received_close_frame_ = false;
  uint16_t received_close_code_ = 0;
  std::string received_close_reason_;

  // The current state of the channel. Mainly used for sanity checking, but also
  // used to track the close state.
  State state_ = FRESHLY_CONSTRUCTED;

  // UTF-8 validator for outgoing Text messages.
  base::StreamingUtf8Validator outgoing_utf8_validator_;
  bool sending_text_message_ = false;

  // UTF-8 validator for incoming Text messages.
  base::StreamingUtf8Validator incoming_utf8_validator_;
  bool receiving_text_message_ = false;

  // True if we are in the middle of receiving a message.
  bool expecting_to_handle_continuation_ = false;

  // True if we have already sent the type (Text or Binary) of the current
  // message to the renderer. This can be false if the message is empty so far.
  bool initial_frame_forwarded_ = false;

  // True if we're waiting for OnReadDone() callback.
  bool is_reading_ = false;
};

}  // namespace net

#endif  // NET_WEBSOCKETS_WEBSOCKET_CHANNEL_H_
