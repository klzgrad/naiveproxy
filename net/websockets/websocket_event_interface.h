// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_WEBSOCKETS_WEBSOCKET_EVENT_INTERFACE_H_
#define NET_WEBSOCKETS_WEBSOCKET_EVENT_INTERFACE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"  // for WARN_UNUSED_RESULT
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"

class GURL;

namespace net {

class IOBuffer;
class SSLInfo;
class URLRequest;
struct WebSocketHandshakeRequestInfo;
struct WebSocketHandshakeResponseInfo;

// Interface for events sent from the network layer to the content layer. These
// events will generally be sent as-is to the renderer process.
class NET_EXPORT WebSocketEventInterface {
 public:
  typedef int WebSocketMessageType;

  // Any event can cause the Channel to be deleted. The Channel needs to avoid
  // doing further processing in this case. It does not need to do cleanup, as
  // cleanup will already have been done as a result of the deletion.
  enum ChannelState {
    CHANNEL_ALIVE,
    CHANNEL_DELETED
  };

  virtual ~WebSocketEventInterface() {}

  // Called when a URLRequest is created for handshaking.
  virtual void OnCreateURLRequest(URLRequest* request) = 0;

  // Called in response to an AddChannelRequest. This means that a response has
  // been received from the remote server.
  virtual ChannelState OnAddChannelResponse(
      const std::string& selected_subprotocol,
      const std::string& extensions) WARN_UNUSED_RESULT = 0;

  // Called when a data frame has been received from the remote host and needs
  // to be forwarded to the renderer process.
  virtual ChannelState OnDataFrame(bool fin,
                                   WebSocketMessageType type,
                                   scoped_refptr<IOBuffer> buffer,
                                   size_t buffer_size) WARN_UNUSED_RESULT = 0;

  // Called to provide more send quota for this channel to the renderer
  // process. Currently the quota units are always bytes of message body
  // data. In future it might depend on the type of multiplexing in use.
  virtual ChannelState OnFlowControl(int64_t quota) WARN_UNUSED_RESULT = 0;

  // Called when the remote server has Started the WebSocket Closing
  // Handshake. The client should not attempt to send any more messages after
  // receiving this message. It will be followed by OnDropChannel() when the
  // closing handshake is complete.
  virtual ChannelState OnClosingHandshake() WARN_UNUSED_RESULT = 0;

  // Called when the channel has been dropped, either due to a network close, a
  // network error, or a protocol error. This may or may not be preceeded by a
  // call to OnClosingHandshake().
  //
  // Warning: Both the |code| and |reason| are passed through to Javascript, so
  // callers must take care not to provide details that could be useful to
  // attackers attempting to use WebSockets to probe networks.
  //
  // |was_clean| should be true if the closing handshake completed successfully.
  //
  // The channel should not be used again after OnDropChannel() has been
  // called.
  //
  // This method returns a ChannelState for consistency, but all implementations
  // must delete the Channel and return CHANNEL_DELETED.
  virtual ChannelState OnDropChannel(bool was_clean,
                                     uint16_t code,
                                     const std::string& reason)
      WARN_UNUSED_RESULT = 0;

  // Called when the browser fails the channel, as specified in the spec.
  //
  // The channel should not be used again after OnFailChannel() has been
  // called.
  //
  // This method returns a ChannelState for consistency, but all implementations
  // must delete the Channel and return CHANNEL_DELETED.
  virtual ChannelState OnFailChannel(const std::string& message)
      WARN_UNUSED_RESULT = 0;

  // Called when the browser starts the WebSocket Opening Handshake.
  virtual ChannelState OnStartOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeRequestInfo> request)
      WARN_UNUSED_RESULT = 0;

  // Called when the browser finishes the WebSocket Opening Handshake.
  virtual ChannelState OnFinishOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeResponseInfo> response)
      WARN_UNUSED_RESULT = 0;

  // Callbacks to be used in response to a call to OnSSLCertificateError. Very
  // similar to content::SSLErrorHandler::Delegate (which we can't use directly
  // due to layering constraints).
  class NET_EXPORT SSLErrorCallbacks {
   public:
    virtual ~SSLErrorCallbacks() {}

    // Cancels the SSL response in response to the error.
    virtual void CancelSSLRequest(int error, const SSLInfo* ssl_info) = 0;

    // Continue with the SSL connection despite the error.
    virtual void ContinueSSLRequest() = 0;
  };

  // Called on SSL Certificate Error during the SSL handshake. Should result in
  // a call to either ssl_error_callbacks->ContinueSSLRequest() or
  // ssl_error_callbacks->CancelSSLRequest(). Normally the implementation of
  // this method will delegate to content::SSLManager::OnSSLCertificateError to
  // make the actual decision. The callbacks must not be called after the
  // WebSocketChannel has been destroyed.
  virtual ChannelState OnSSLCertificateError(
      std::unique_ptr<SSLErrorCallbacks> ssl_error_callbacks,
      const GURL& url,
      const SSLInfo& ssl_info,
      bool fatal) WARN_UNUSED_RESULT = 0;

 protected:
  WebSocketEventInterface() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(WebSocketEventInterface);
};

}  // namespace net

#endif  // NET_WEBSOCKETS_WEBSOCKET_EVENT_INTERFACE_H_
