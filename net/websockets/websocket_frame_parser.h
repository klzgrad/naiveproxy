// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_WEBSOCKETS_WEBSOCKET_FRAME_PARSER_H_
#define NET_WEBSOCKETS_WEBSOCKET_FRAME_PARSER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/websockets/websocket_errors.h"
#include "net/websockets/websocket_frame.h"

namespace net {

// Parses WebSocket frames from byte stream.
//
// Specification of WebSocket frame format is available at
// <http://tools.ietf.org/html/rfc6455#section-5>.

class NET_EXPORT WebSocketFrameParser {
 public:
  WebSocketFrameParser();
  ~WebSocketFrameParser();

  // Decodes the given byte stream and stores parsed WebSocket frames in
  // |frame_chunks|.
  //
  // If the parser encounters invalid payload length format, Decode() fails
  // and returns false. Once Decode() has failed, the parser refuses to decode
  // any more data and future invocations of Decode() will simply return false.
  //
  // Payload data of parsed WebSocket frames may be incomplete; see comments in
  // websocket_frame.h for more details.
  bool Decode(const char* data,
              size_t length,
              std::vector<std::unique_ptr<WebSocketFrameChunk>>* frame_chunks);

  // Returns kWebSocketNormalClosure if the parser has not failed to decode
  // WebSocket frames. Otherwise returns WebSocketError which is defined in
  // websocket_errors.h. We can convert net::WebSocketError to net::Error by
  // using WebSocketErrorToNetError().
  WebSocketError websocket_error() const { return websocket_error_; }

 private:
  // Tries to decode a frame header from |current_read_pos_|.
  // If successful, this function updates |current_read_pos_|,
  // |current_frame_header_|, and |masking_key_| (if available).
  // This function may set |failed_| to true if it observes a corrupt frame.
  // If there is not enough data in the remaining buffer to parse a frame
  // header, this function returns without doing anything.
  void DecodeFrameHeader();

  // Decodes frame payload and creates a WebSocketFrameChunk object.
  // This function updates |current_read_pos_| and |frame_offset_| after
  // parsing. This function returns a frame object even if no payload data is
  // available at this moment, so the receiver could make use of frame header
  // information. If the end of frame is reached, this function clears
  // |current_frame_header_|, |frame_offset_| and |masking_key_|.
  std::unique_ptr<WebSocketFrameChunk> DecodeFramePayload(bool first_chunk);

  // Internal buffer to store the data to parse.
  std::vector<char> buffer_;

  // Position in |buffer_| where the next round of parsing starts.
  size_t current_read_pos_;

  // Frame header and masking key of the current frame.
  // |masking_key_| is filled with zeros if the current frame is not masked.
  std::unique_ptr<WebSocketFrameHeader> current_frame_header_;
  WebSocketMaskingKey masking_key_;

  // Amount of payload data read so far for the current frame.
  uint64_t frame_offset_;

  WebSocketError websocket_error_;

  DISALLOW_COPY_AND_ASSIGN(WebSocketFrameParser);
};

}  // namespace net

#endif  // NET_WEBSOCKETS_WEBSOCKET_FRAME_PARSER_H_
