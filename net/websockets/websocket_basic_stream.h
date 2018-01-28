// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_WEBSOCKETS_WEBSOCKET_BASIC_STREAM_H_
#define NET_WEBSOCKETS_WEBSOCKET_BASIC_STREAM_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/websockets/websocket_frame_parser.h"
#include "net/websockets/websocket_stream.h"

namespace net {

class ClientSocketHandle;
class DrainableIOBuffer;
class GrowableIOBuffer;
class IOBufferWithSize;
struct WebSocketFrame;
struct WebSocketFrameChunk;

// Implementation of WebSocketStream for non-multiplexed ws:// connections (or
// the physical side of a multiplexed ws:// connection).
class NET_EXPORT_PRIVATE WebSocketBasicStream : public WebSocketStream {
 public:
  typedef WebSocketMaskingKey (*WebSocketMaskingKeyGeneratorFunction)();

  // This class should not normally be constructed directly; see
  // WebSocketStream::CreateAndConnectStream() and
  // WebSocketBasicHandshakeStream::Upgrade().
  WebSocketBasicStream(std::unique_ptr<ClientSocketHandle> connection,
                       const scoped_refptr<GrowableIOBuffer>& http_read_buffer,
                       const std::string& sub_protocol,
                       const std::string& extensions);

  // The destructor has to make sure the connection is closed when we finish so
  // that it does not get returned to the pool.
  ~WebSocketBasicStream() override;

  // WebSocketStream implementation.
  int ReadFrames(std::vector<std::unique_ptr<WebSocketFrame>>* frames,
                 const CompletionCallback& callback) override;

  int WriteFrames(std::vector<std::unique_ptr<WebSocketFrame>>* frames,
                  const CompletionCallback& callback) override;

  void Close() override;

  std::string GetSubProtocol() const override;

  std::string GetExtensions() const override;

  ////////////////////////////////////////////////////////////////////////////
  // Methods for testing only.

  static std::unique_ptr<WebSocketBasicStream>
  CreateWebSocketBasicStreamForTesting(
      std::unique_ptr<ClientSocketHandle> connection,
      const scoped_refptr<GrowableIOBuffer>& http_read_buffer,
      const std::string& sub_protocol,
      const std::string& extensions,
      WebSocketMaskingKeyGeneratorFunction key_generator_function);

 private:
  // Returns OK or calls |callback| when the |buffer| is fully drained or
  // something has failed.
  int WriteEverything(const scoped_refptr<DrainableIOBuffer>& buffer,
                      const CompletionCallback& callback);

  // Wraps the |callback| to continue writing until everything has been written.
  void OnWriteComplete(const scoped_refptr<DrainableIOBuffer>& buffer,
                       const CompletionCallback& callback,
                       int result);

  // Attempts to parse the output of a read as WebSocket frames. On success,
  // returns OK and places the frame(s) in |frames|.
  int HandleReadResult(int result,
                       std::vector<std::unique_ptr<WebSocketFrame>>* frames);

  // Converts the chunks in |frame_chunks| into frames and writes them to
  // |frames|. |frame_chunks| is destroyed in the process. Returns
  // ERR_WS_PROTOCOL_ERROR if an invalid chunk was found. If one or more frames
  // was added to |frames|, then returns OK, otherwise returns ERR_IO_PENDING.
  int ConvertChunksToFrames(
      std::vector<std::unique_ptr<WebSocketFrameChunk>>* frame_chunks,
      std::vector<std::unique_ptr<WebSocketFrame>>* frames);

  // Converts a |chunk| to a |frame|. |*frame| should be NULL on entry to this
  // method. If |chunk| is an incomplete control frame, or an empty middle
  // frame, then |*frame| may still be NULL on exit. If an invalid control frame
  // is found, returns ERR_WS_PROTOCOL_ERROR and the stream is no longer
  // usable. Otherwise returns OK (even if frame is still NULL).
  int ConvertChunkToFrame(std::unique_ptr<WebSocketFrameChunk> chunk,
                          std::unique_ptr<WebSocketFrame>* frame);

  // Creates a frame based on the value of |is_final_chunk|, |data| and
  // |current_frame_header_|. Clears |current_frame_header_| if |is_final_chunk|
  // is true. |data| may be NULL if the frame has an empty payload. A frame in
  // the middle of a message with no data is not useful; in this case the
  // returned frame will be NULL. Otherwise, |current_frame_header_->opcode| is
  // set to Continuation after use if it was Text or Binary, in accordance with
  // WebSocket RFC6455 section 5.4.
  std::unique_ptr<WebSocketFrame> CreateFrame(
      bool is_final_chunk,
      const scoped_refptr<IOBufferWithSize>& data);

  // Adds |data_buffer| to the end of |incomplete_control_frame_body_|, applying
  // bounds checks.
  void AddToIncompleteControlFrameBody(
      const scoped_refptr<IOBufferWithSize>& data_buffer);

  // Called when a read completes. Parses the result and (unless no complete
  // header has been received) calls |callback|.
  void OnReadComplete(std::vector<std::unique_ptr<WebSocketFrame>>* frames,
                      const CompletionCallback& callback,
                      int result);

  // Storage for pending reads. All active WebSockets spend all the time with a
  // call to ReadFrames() pending, so there is no benefit in trying to share
  // this between sockets.
  scoped_refptr<IOBufferWithSize> read_buffer_;

  // The connection, wrapped in a ClientSocketHandle so that we can prevent it
  // from being returned to the pool.
  std::unique_ptr<ClientSocketHandle> connection_;

  // Frame header for the frame currently being received. Only non-NULL while we
  // are processing the frame. If the frame arrives in multiple chunks, it can
  // remain non-NULL until additional chunks arrive. If the header of the frame
  // was invalid, this is set to NULL, the channel is failed, and subsequent
  // chunks of the same frame will be ignored.
  std::unique_ptr<WebSocketFrameHeader> current_frame_header_;

  // Although it should rarely happen in practice, a control frame can arrive
  // broken into chunks. This variable provides storage for a partial control
  // frame until the rest arrives. It will be NULL the rest of the time.
  scoped_refptr<GrowableIOBuffer> incomplete_control_frame_body_;

  // Only used during handshake. Some data may be left in this buffer after the
  // handshake, in which case it will be picked up during the first call to
  // ReadFrames(). The type is GrowableIOBuffer for compatibility with
  // net::HttpStreamParser, which is used to parse the handshake.
  scoped_refptr<GrowableIOBuffer> http_read_buffer_;

  // This keeps the current parse state (including any incomplete headers) and
  // parses frames.
  WebSocketFrameParser parser_;

  // The negotated sub-protocol, or empty for none.
  const std::string sub_protocol_;

  // The extensions negotiated with the remote server.
  const std::string extensions_;

  // This can be overridden in tests to make the output deterministic. We don't
  // use a Callback here because a function pointer is faster and good enough
  // for our purposes.
  WebSocketMaskingKeyGeneratorFunction generate_websocket_masking_key_;
};

}  // namespace net

#endif  // NET_WEBSOCKETS_WEBSOCKET_BASIC_STREAM_H_
