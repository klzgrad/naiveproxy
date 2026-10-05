// Copyright 2026 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/websocket_frame_buffer.h"

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify the buffer arithmetic.
#pragma allow_unsafe_buffers
#endif

#include <algorithm>
#include <utility>

#include "base/numerics/safe_conversions.h"

namespace net {
namespace {

bool IsData(const WebSocketFrame& frame) {
  return frame.header.opcode == WebSocketFrameHeader::kOpCodeBinary ||
         frame.header.opcode == WebSocketFrameHeader::kOpCodeContinuation;
}

}  // namespace

WebSocketFrameBuffer::WebSocketFrameBuffer() = default;

WebSocketFrameBuffer::~WebSocketFrameBuffer() = default;

void WebSocketFrameBuffer::Append(
    std::vector<std::unique_ptr<WebSocketFrame>> frames) {
  frames_.insert(frames_.end(), std::make_move_iterator(frames.begin()),
                 std::make_move_iterator(frames.end()));
  frames.clear();
}

bool WebSocketFrameBuffer::TakeStatus(uint8_t* status) {
  for (auto it = frames_.begin(); it != frames_.end(); ++it) {
    if ((*it)->header.opcode != WebSocketFrameHeader::kOpCodeBinary ||
        (*it)->payload.empty()) {
      continue;
    }
    *status = (*it)->payload[0];
    frames_.erase(it);
    return true;
  }
  return false;
}

WebSocketFrameBuffer::ControlFrames
WebSocketFrameBuffer::ProcessControlFrames() {
  ControlFrames result;
  for (auto it = frames_.begin(); it != frames_.end();) {
    const auto opcode = (*it)->header.opcode;
    if (opcode == WebSocketFrameHeader::kOpCodePing) {
      result.ping_payload.emplace((*it)->payload.begin(),
                                  (*it)->payload.end());
      it = frames_.erase(it);
    } else if (opcode == WebSocketFrameHeader::kOpCodePong) {
      it = frames_.erase(it);
    } else if (opcode == WebSocketFrameHeader::kOpCodeClose) {
      result.closed = true;
      return result;
    } else {
      ++it;
    }
  }
  return result;
}

int WebSocketFrameBuffer::Read(IOBuffer* buffer, int length) {
  int copied = 0;
  for (auto it = frames_.begin(); it != frames_.end();) {
    if (!IsData(**it)) {
      it = frames_.erase(it);
      continue;
    }
    const size_t available = (*it)->payload.size() - read_offset_;
    const size_t space = base::checked_cast<size_t>(length - copied);
    const size_t count = std::min(available, space);
    std::memcpy(buffer->data() + copied, (*it)->payload.data() + read_offset_,
                count);
    copied += base::checked_cast<int>(count);
    read_offset_ += count;
    if (read_offset_ == (*it)->payload.size()) {
      it = frames_.erase(it);
      read_offset_ = 0;
    } else {
      // A partial frame can only remain when the caller's buffer is full.
      break;
    }
    if (copied == length) {
      break;
    }
  }
  return copied;
}

void WebSocketFrameBuffer::Clear() {
  frames_.clear();
  read_offset_ = 0;
}

}  // namespace net
