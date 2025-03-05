// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_WEBSOCKETS_WEBSOCKET_BASIC_STREAM_H_
#define NET_WEBSOCKETS_WEBSOCKET_BASIC_STREAM_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/containers/heap_array.h"
#include "base/containers/queue.h"
#include "base/containers/span.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/net_export.h"
#include "net/log/net_log_with_source.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/websockets/websocket_chunk_assembler.h"
#include "net/websockets/websocket_frame.h"
#include "net/websockets/websocket_frame_parser.h"
#include "net/websockets/websocket_stream.h"

namespace base {
class TimeTicks;
}  // namespace base

namespace net {

class ClientSocketHandle;
class DrainableIOBuffer;
class GrowableIOBuffer;
class IOBuffer;
class IOBufferWithSize;
struct WebSocketFrame;
struct WebSocketFrameChunk;
struct NetworkTrafficAnnotationTag;

// Implementation of WebSocketStream for non-multiplexed ws:// connections (or
// the physical side of a multiplexed ws:// connection).
//
// Please update the traffic annotations in the websocket_basic_stream.cc and
// websocket_stream.cc if the class is used for any communication with Google.
// In such a case, annotation should be passed from the callers to this class
// and a local annotation can not be used anymore.
class NET_EXPORT_PRIVATE WebSocketBasicStream final : public WebSocketStream {
 public:
  typedef WebSocketMaskingKey (*WebSocketMaskingKeyGeneratorFunction)();

  enum class BufferSize : uint8_t {
    kSmall,
    kLarge,
  };

  // A class that calculates whether the associated WebSocketBasicStream
  // should use a small buffer or large buffer, given the timing information
  // or Read calls. This class is public for testing.
  class NET_EXPORT_PRIVATE BufferSizeManager final {
   public:
    BufferSizeManager();
    BufferSizeManager(const BufferSizeManager&) = delete;
    BufferSizeManager& operator=(const BufferSizeManager&) = delete;
    ~BufferSizeManager();

    // Called when the associated WebSocketBasicStream starts reading data
    // into a buffer.
    void OnRead(base::TimeTicks now);

    // Called when the Read operation completes. `size` must be positive.
    void OnReadComplete(base::TimeTicks now, int size);

    // Returns the appropriate buffer size the associated WebSocketBasicStream
    // should use.
    BufferSize buffer_size() const { return buffer_size_; }

    // Set the rolling average window for tests.
    void set_window_for_test(size_t size) { rolling_average_window_ = size; }

   private:
    // This keeps the best read buffer size.
    BufferSize buffer_size_ = BufferSize::kSmall;

    // The number of results to calculate the throughput. This is a variable so
    // that unittests can set other values.
    size_t rolling_average_window_ = 100;

    // This keeps the timestamps to calculate the throughput.
    base::queue<base::TimeTicks> read_start_timestamps_;

    // The sum of the last few read size.
    int rolling_byte_total_ = 0;

    // This keeps the read size.
    base::queue<int> recent_read_sizes_;
  };

  // Adapter that allows WebSocketBasicStream to use
  // either a TCP/IP or TLS socket, or an HTTP/2 stream.
  class Adapter {
   public:
    virtual ~Adapter() = default;
    virtual int Read(IOBuffer* buf,
                     int buf_len,
                     CompletionOnceCallback callback) = 0;
    virtual int Write(
        IOBuffer* buf,
        int buf_len,
        CompletionOnceCallback callback,
        const NetworkTrafficAnnotationTag& traffic_annotation) = 0;
    virtual void Disconnect() = 0;
    virtual bool is_initialized() const = 0;
  };

  // This class should not normally be constructed directly; see
  // WebSocketStream::CreateAndConnectStream() and
  // WebSocketBasicHandshakeStream::Upgrade().
  WebSocketBasicStream(std::unique_ptr<Adapter> connection,
                       const scoped_refptr<GrowableIOBuffer>& http_read_buffer,
                       const std::string& sub_protocol,
                       const std::string& extensions,
                       const NetLogWithSource& net_log);

  // The destructor has to make sure the connection is closed when we finish so
  // that it does not get returned to the pool.
  ~WebSocketBasicStream() override;

  // WebSocketStream implementation.
  int ReadFrames(std::vector<std::unique_ptr<WebSocketFrame>>* frames,
                 CompletionOnceCallback callback) override;

  int WriteFrames(std::vector<std::unique_ptr<WebSocketFrame>>* frames,
                  CompletionOnceCallback callback) override;

  void Close() override;

  std::string GetSubProtocol() const override;

  std::string GetExtensions() const override;

  const NetLogWithSource& GetNetLogWithSource() const override;

  ////////////////////////////////////////////////////////////////////////////
  // Methods for testing only.

  static std::unique_ptr<WebSocketBasicStream>
  CreateWebSocketBasicStreamForTesting(
      std::unique_ptr<ClientSocketHandle> connection,
      const scoped_refptr<GrowableIOBuffer>& http_read_buffer,
      const std::string& sub_protocol,
      const std::string& extensions,
      const NetLogWithSource& net_log,
      WebSocketMaskingKeyGeneratorFunction key_generator_function);

 private:
  // Reads until socket read returns asynchronously or returns error.
  // If returns ERR_IO_PENDING, then |read_callback_| will be called with result
  // later.
  int ReadEverything(std::vector<std::unique_ptr<WebSocketFrame>>* frames);

  // Called when a read completes. Parses the result, tries to read more.
  // Might call |read_callback_|.
  void OnReadComplete(std::vector<std::unique_ptr<WebSocketFrame>>* frames,
                      int result);

  // Writes until |buffer| is fully drained (in which case returns OK) or a
  // socket write returns asynchronously or returns an error.  If returns
  // ERR_IO_PENDING, then |write_callback_| will be called with result later.
  int WriteEverything(const scoped_refptr<DrainableIOBuffer>& buffer);

  // Called when a write completes.  Tries to write more.
  // Might call |write_callback_|.
  void OnWriteComplete(const scoped_refptr<DrainableIOBuffer>& buffer,
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

  // Storage for pending reads.
  scoped_refptr<IOBufferWithSize> read_buffer_;

  // The best read buffer size for the current throughput.
  size_t target_read_buffer_size_;

  // The connection, wrapped in a ClientSocketHandle so that we can prevent it
  // from being returned to the pool.
  std::unique_ptr<Adapter> connection_;

  // Storage for payload of multiple control frames.
  std::vector<base::HeapArray<uint8_t>> control_frame_payloads_;

  // Only used during handshake. Some data may be left in this buffer after the
  // handshake, in which case it will be picked up during the first call to
  // ReadFrames(). The type is GrowableIOBuffer for compatibility with
  // net::HttpStreamParser, which is used to parse the handshake.
  scoped_refptr<GrowableIOBuffer> http_read_buffer_;
  // Flag to keep above buffer until next ReadFrames() after decoding.
  bool is_http_read_buffer_decoded_ = false;

  // This keeps the current parse state (including any incomplete headers) and
  // parses frames.
  WebSocketFrameParser parser_;

  // The negotated sub-protocol, or empty for none.
  const std::string sub_protocol_;

  // The extensions negotiated with the remote server.
  const std::string extensions_;

  NetLogWithSource net_log_;

  // This is used for adaptive read buffer size.
  BufferSizeManager buffer_size_manager_;

  // This keeps the current read buffer size.
  BufferSize buffer_size_ = buffer_size_manager_.buffer_size();

  // This can be overridden in tests to make the output deterministic. We don't
  // use a Callback here because a function pointer is faster and good enough
  // for our purposes.
  WebSocketMaskingKeyGeneratorFunction generate_websocket_masking_key_;

  // User callback saved for asynchronous writes and reads.
  CompletionOnceCallback write_callback_;
  CompletionOnceCallback read_callback_;

  // Used to assemble FrameChunks into Frames.
  WebSocketChunkAssembler chunk_assembler_;
};

}  // namespace net

#endif  // NET_WEBSOCKETS_WEBSOCKET_BASIC_STREAM_H_
