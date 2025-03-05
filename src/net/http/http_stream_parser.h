// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_STREAM_PARSER_H_
#define NET_HTTP_HTTP_STREAM_PARSER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "crypto/ec_private_key.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/base/upload_data_stream.h"
#include "net/log/net_log_with_source.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace net {

class DrainableIOBuffer;
class GrowableIOBuffer;
class HttpChunkedDecoder;
class HttpRequestHeaders;
class HttpResponseInfo;
class IOBuffer;
class StreamSocket;
class UploadDataStream;

class NET_EXPORT_PRIVATE HttpStreamParser {
 public:
  // |connection_is_reused| must be |true| if |stream_socket| has previously
  // been used successfully for an HTTP/1.x request.
  //
  // Any data in |read_buffer| will be used before reading from the socket
  // and any data left over after parsing the stream will be put into
  // |read_buffer|.  The left over data will start at offset 0 and the
  // buffer's offset will be set to the first free byte. |read_buffer| may
  // have its capacity changed.
  //
  // It is not safe to call into the HttpStreamParser after destroying the
  // |stream_socket|.
  //
  // `upload_data_stream` must remain valid until the SendRequest() callback is
  // invoked or the HttpStreamParser has been destroyed.
  HttpStreamParser(StreamSocket* stream_socket,
                   bool connection_is_reused,
                   const GURL& url,
                   const std::string& method,
                   UploadDataStream* upload_data_stream,
                   GrowableIOBuffer* read_buffer,
                   const NetLogWithSource& net_log);

  HttpStreamParser(const HttpStreamParser&) = delete;
  HttpStreamParser& operator=(const HttpStreamParser&) = delete;

  virtual ~HttpStreamParser();

  // These functions implement the interface described in HttpStream with
  // some additional functionality
  int SendRequest(const std::string& request_line,
                  const HttpRequestHeaders& headers,
                  const NetworkTrafficAnnotationTag& traffic_annotation,
                  HttpResponseInfo* response,
                  CompletionOnceCallback callback);

  int ConfirmHandshake(CompletionOnceCallback callback);

  int ReadResponseHeaders(CompletionOnceCallback callback);

  int ReadResponseBody(IOBuffer* buf,
                       int buf_len,
                       CompletionOnceCallback callback);

  bool IsResponseBodyComplete() const;

  bool CanFindEndOfResponse() const;

  bool IsMoreDataBuffered() const;

  // Returns true if the underlying connection can be reused.
  // The connection can be reused if:
  // * It's still connected.
  // * The response headers indicate the connection can be kept alive.
  // * The end of the response can be found, though it may not have yet been
  //     received.
  //
  // Note that if response headers have yet to be received, this will return
  // false.
  bool CanReuseConnection() const;

  // Called when stream is closed.
  void OnConnectionClose();

  const GURL& url() { return url_; }
  const std::string& method() { return method_; }

  int64_t received_bytes() const { return received_bytes_; }

  int64_t sent_bytes() const { return sent_bytes_; }

  base::TimeTicks first_response_start_time() const {
    return first_response_start_time_;
  }
  base::TimeTicks non_informational_response_start_time() const {
    return non_informational_response_start_time_;
  }
  base::TimeTicks first_early_hints_time() { return first_early_hints_time_; }

  // Encodes the given |payload| in the chunked format to |output|.
  // Returns the number of bytes written to |output|. output.size() should
  // be large enough to store the encoded chunk, which is payload.size() +
  // kChunkHeaderFooterSize. Returns ERR_INVALID_ARGUMENT if output.size()
  // is not large enough.
  //
  // The output will look like: "HEX\r\n[payload]\r\n"
  // where HEX is a length in hexadecimal (without the "0x" prefix).
  static int EncodeChunk(std::string_view payload, base::span<uint8_t> output);

  // Returns true if request headers and body should be merged (i.e. the
  // sum is small enough and the body is in memory, and not chunked).
  static bool ShouldMergeRequestHeadersAndBody(
      const std::string& request_headers,
      const UploadDataStream* request_body);

  // The number of extra bytes required to encode a chunk.
  static const size_t kChunkHeaderFooterSize;

 private:
  class SeekableIOBuffer;

  // FOO_COMPLETE states implement the second half of potentially asynchronous
  // operations and don't necessarily mean that FOO is complete.
  enum State {
    // STATE_NONE indicates that this is waiting on an external call before
    // continuing.
    STATE_NONE,
    STATE_SEND_HEADERS,
    STATE_SEND_HEADERS_COMPLETE,
    STATE_SEND_BODY,
    STATE_SEND_BODY_COMPLETE,
    STATE_SEND_REQUEST_READ_BODY_COMPLETE,
    STATE_SEND_REQUEST_COMPLETE,
    STATE_READ_HEADERS,
    STATE_READ_HEADERS_COMPLETE,
    STATE_READ_BODY,
    STATE_READ_BODY_COMPLETE,
    STATE_DONE
  };

  // The number of bytes by which the header buffer is grown when it reaches
  // capacity.
  static const int kHeaderBufInitialSize = 4 * 1024;  // 4K

  // |kMaxHeaderBufSize| is the number of bytes that the response headers can
  // grow to. If the body start is not found within this range of the
  // response, the transaction will fail with ERR_RESPONSE_HEADERS_TOO_BIG.
  // Note: |kMaxHeaderBufSize| should be a multiple of |kHeaderBufInitialSize|.
  static const int kMaxHeaderBufSize = kHeaderBufInitialSize * 64;  // 256K

  // The maximum sane buffer size.
  static const int kMaxBufSize = 2 * 1024 * 1024;  // 2M

  // Handle callbacks.
  void OnIOComplete(int result);

  // Try to make progress sending/receiving the request/response.
  int DoLoop(int result);

  // The implementations of each state of the state machine.
  int DoSendHeaders();
  int DoSendHeadersComplete(int result);
  int DoSendBody();
  int DoSendBodyComplete(int result);
  int DoSendRequestReadBodyComplete(int result);
  int DoSendRequestComplete(int result);
  int DoReadHeaders();
  int DoReadHeadersComplete(int result);
  int DoReadBody();
  int DoReadBodyComplete(int result);

  // This handles most of the logic for DoReadHeadersComplete.
  int HandleReadHeaderResult(int result);

  void RunConfirmHandshakeCallback(int rv);

  // Examines |read_buf_| to find the start and end of the headers. If they are
  // found, parse them with DoParseResponseHeaders().  Return the offset for
  // the end of the headers, or -1 if the complete headers were not found, or
  // with a net::Error if we encountered an error during parsing.
  //
  // |new_bytes| is the number of new bytes that have been appended to the end
  // of |read_buf_| since the last call to this method (which must have returned
  // -1).
  int FindAndParseResponseHeaders(int new_bytes);

  // Parse the headers into response_.  Returns OK on success or a net::Error on
  // failure.
  int ParseResponseHeaders(size_t end_of_header_offset);

  // Examine the parsed headers to try to determine the response body size.
  void CalculateResponseBodySize();

  // Check if buffers used to send the request are empty.
  bool SendRequestBuffersEmpty();

  // Next state of the request, when the current one completes.
  State io_state_ = STATE_NONE;

  const GURL url_;
  const std::string method_;

  // Only non-null while writing the request headers and body.
  raw_ptr<UploadDataStream> upload_data_stream_;

  // The request header data.  May include a merged request body.
  scoped_refptr<DrainableIOBuffer> request_headers_;

  // Size of just the request headers.  May be less than the length of
  // |request_headers_| if the body was merged with the headers.
  int request_headers_length_ = 0;

  // Temporary buffer for reading.
  scoped_refptr<GrowableIOBuffer> read_buf_;

  // Offset of the first unused byte in |read_buf_|.  May be nonzero due to
  // body data in the same packet as header data but is zero when reading
  // headers.
  size_t read_buf_unused_offset_ = 0;

  // The amount beyond |read_buf_unused_offset_| where the status line starts;
  // std::string::npos if not found yet.
  size_t response_header_start_offset_;

  // The amount of received data.  If connection is reused then intermediate
  // value may be bigger than final.
  int64_t received_bytes_ = 0;

  // The amount of sent data.
  int64_t sent_bytes_ = 0;

  // The parsed response headers.  Owned by the caller of SendRequest.   This
  // cannot be safely accessed after reading the final set of headers, as the
  // caller of SendRequest may have been destroyed - this happens in the case an
  // HttpResponseBodyDrainer is used.
  raw_ptr<HttpResponseInfo> response_ = nullptr;

  // Time at which the first bytes of the first header response including
  // informational responses (1xx) are about to be parsed. This corresponds to
  // |LoadTimingInfo::receive_headers_start|. See also comments there.
  base::TimeTicks first_response_start_time_;

  // Time at which the first bytes of the current header response are about to
  // be parsed. This is reset every time new response headers including
  // non-informational responses (1xx) are parsed.
  base::TimeTicks current_response_start_time_;

  // Time at which the first byte of the non-informational header response
  // (non-1xx) are about to be parsed. This corresponds to
  // |LoadTimingInfo::receive_non_informational_headers_start|. See also
  // comments there.
  base::TimeTicks non_informational_response_start_time_;

  // Time at which the first 103 Early Hints response is received. This
  // corresponds to |LoadTimingInfo::first_early_hints_time|.
  base::TimeTicks first_early_hints_time_;

  // Indicates the content length.  If this value is less than zero
  // (and chunked_decoder_ is null), then we must read until the server
  // closes the connection.
  int64_t response_body_length_ = -1;

  // True if reading a keep-alive response. False if not, or if don't yet know.
  bool response_is_keep_alive_ = false;

  // True if we've seen a response that has an HTTP status line. This is
  // persistent across multiple response parsing. If we see a status line
  // for a response, this will remain true forever.
  bool has_seen_status_line_ = false;

  // Keep track of the number of response body bytes read so far.
  int64_t response_body_read_ = 0;

  // Helper if the data is chunked.
  std::unique_ptr<HttpChunkedDecoder> chunked_decoder_;

  // Where the caller wants the body data.
  scoped_refptr<IOBuffer> user_read_buf_;
  size_t user_read_buf_len_ = 0;

  // The callback to notify a user that the handshake has been confirmed.
  CompletionOnceCallback confirm_handshake_callback_;

  // The callback to notify a user that their request or response is
  // complete or there was an error
  CompletionOnceCallback callback_;

  // The underlying socket, owned by the caller. The HttpStreamParser must be
  // destroyed before the caller destroys the socket, or relinquishes ownership
  // of it.
  raw_ptr<StreamSocket> stream_socket_;

  // Whether the socket has already been used. Only used in HTTP/0.9 detection
  // logic.
  const bool connection_is_reused_;

  NetLogWithSource net_log_;

  // Callback to be used when doing IO.
  CompletionRepeatingCallback io_callback_;

  // Buffer used to read the request body from UploadDataStream.
  scoped_refptr<SeekableIOBuffer> request_body_read_buf_;
  // Buffer used to send the request body. This points the same buffer as
  // |request_body_read_buf_| unless the data is chunked.
  scoped_refptr<SeekableIOBuffer> request_body_send_buf_;
  bool sent_last_chunk_ = false;

  // Whether the Content-Length was known and extra data was discarded.
  bool discarded_extra_data_ = false;

  // Whether the response body should be truncated to the Content-Length.
  const bool truncate_to_content_length_enabled_;

  // Error received when uploading the body, if any.
  int upload_error_ = OK;

  MutableNetworkTrafficAnnotationTag traffic_annotation_;

  base::WeakPtrFactory<HttpStreamParser> weak_ptr_factory_{this};
};

}  // namespace net

#endif  // NET_HTTP_HTTP_STREAM_PARSER_H_
