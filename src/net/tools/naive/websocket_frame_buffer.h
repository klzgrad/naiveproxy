// Copyright 2026 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_WEBSOCKET_FRAME_BUFFER_H_
#define NET_TOOLS_NAIVE_WEBSOCKET_FRAME_BUFFER_H_

#include <memory>
#include <optional>
#include <vector>

#include "net/base/io_buffer.h"
#include "net/websockets/websocket_frame.h"

namespace net {

// Presents decoded WebSocket frames to Naive as a byte stream while keeping
// partially consumed frames intact.
class WebSocketFrameBuffer {
 public:
  struct ControlFrames {
    bool closed = false;
    std::optional<std::vector<uint8_t>> ping_payload;
  };

  WebSocketFrameBuffer();
  ~WebSocketFrameBuffer();

  WebSocketFrameBuffer(const WebSocketFrameBuffer&) = delete;
  WebSocketFrameBuffer& operator=(const WebSocketFrameBuffer&) = delete;

  void Append(std::vector<std::unique_ptr<WebSocketFrame>> frames);
  bool TakeStatus(uint8_t* status);
  ControlFrames ProcessControlFrames();
  int Read(IOBuffer* buffer, int length);
  bool empty() const { return frames_.empty(); }
  void Clear();

 private:
  std::vector<std::unique_ptr<WebSocketFrame>> frames_;
  size_t read_offset_ = 0;
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_WEBSOCKET_FRAME_BUFFER_H_
