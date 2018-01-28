// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_frame_parser.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "base/big_endian.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "net/base/io_buffer.h"
#include "net/websockets/websocket_frame.h"

namespace {

const uint8_t kFinalBit = 0x80;
const uint8_t kReserved1Bit = 0x40;
const uint8_t kReserved2Bit = 0x20;
const uint8_t kReserved3Bit = 0x10;
const uint8_t kOpCodeMask = 0xF;
const uint8_t kMaskBit = 0x80;
const uint8_t kPayloadLengthMask = 0x7F;
const uint64_t kMaxPayloadLengthWithoutExtendedLengthField = 125;
const uint64_t kPayloadLengthWithTwoByteExtendedLengthField = 126;
const uint64_t kPayloadLengthWithEightByteExtendedLengthField = 127;

}  // namespace.

namespace net {

WebSocketFrameParser::WebSocketFrameParser()
    : current_read_pos_(0),
      frame_offset_(0),
      websocket_error_(kWebSocketNormalClosure) {
  std::fill(masking_key_.key,
            masking_key_.key + WebSocketFrameHeader::kMaskingKeyLength,
            '\0');
}

WebSocketFrameParser::~WebSocketFrameParser() = default;

bool WebSocketFrameParser::Decode(
    const char* data,
    size_t length,
    std::vector<std::unique_ptr<WebSocketFrameChunk>>* frame_chunks) {
  if (websocket_error_ != kWebSocketNormalClosure)
    return false;
  if (!length)
    return true;

  // TODO(yutak): Remove copy.
  buffer_.insert(buffer_.end(), data, data + length);

  while (current_read_pos_ < buffer_.size()) {
    bool first_chunk = false;
    if (!current_frame_header_.get()) {
      DecodeFrameHeader();
      if (websocket_error_ != kWebSocketNormalClosure)
        return false;
      // If frame header is incomplete, then carry over the remaining
      // data to the next round of Decode().
      if (!current_frame_header_.get())
        break;
      first_chunk = true;
    }

    std::unique_ptr<WebSocketFrameChunk> frame_chunk =
        DecodeFramePayload(first_chunk);
    DCHECK(frame_chunk.get());
    frame_chunks->push_back(std::move(frame_chunk));

    if (current_frame_header_.get()) {
      DCHECK(current_read_pos_ == buffer_.size());
      break;
    }
  }

  // Drain unnecessary data. TODO(yutak): Remove copy. (but how?)
  buffer_.erase(buffer_.begin(), buffer_.begin() + current_read_pos_);
  current_read_pos_ = 0;

  // Sanity check: the size of carried-over data should not exceed
  // the maximum possible length of a frame header.
  static const size_t kMaximumFrameHeaderSize =
      WebSocketFrameHeader::kBaseHeaderSize +
      WebSocketFrameHeader::kMaximumExtendedLengthSize +
      WebSocketFrameHeader::kMaskingKeyLength;
  DCHECK_LT(buffer_.size(), kMaximumFrameHeaderSize);

  return true;
}

void WebSocketFrameParser::DecodeFrameHeader() {
  typedef WebSocketFrameHeader::OpCode OpCode;
  static const int kMaskingKeyLength = WebSocketFrameHeader::kMaskingKeyLength;

  DCHECK(!current_frame_header_.get());

  const char* start = &buffer_.front() + current_read_pos_;
  const char* current = start;
  const char* end = &buffer_.front() + buffer_.size();

  // Header needs 2 bytes at minimum.
  if (end - current < 2)
    return;

  uint8_t first_byte = *current++;
  uint8_t second_byte = *current++;

  bool final = (first_byte & kFinalBit) != 0;
  bool reserved1 = (first_byte & kReserved1Bit) != 0;
  bool reserved2 = (first_byte & kReserved2Bit) != 0;
  bool reserved3 = (first_byte & kReserved3Bit) != 0;
  OpCode opcode = first_byte & kOpCodeMask;

  bool masked = (second_byte & kMaskBit) != 0;
  uint64_t payload_length = second_byte & kPayloadLengthMask;
  if (payload_length == kPayloadLengthWithTwoByteExtendedLengthField) {
    if (end - current < 2)
      return;
    uint16_t payload_length_16;
    base::ReadBigEndian(current, &payload_length_16);
    current += 2;
    payload_length = payload_length_16;
    if (payload_length <= kMaxPayloadLengthWithoutExtendedLengthField)
      websocket_error_ = kWebSocketErrorProtocolError;
  } else if (payload_length == kPayloadLengthWithEightByteExtendedLengthField) {
    if (end - current < 8)
      return;
    base::ReadBigEndian(current, &payload_length);
    current += 8;
    if (payload_length <= UINT16_MAX ||
        payload_length > static_cast<uint64_t>(INT64_MAX)) {
      websocket_error_ = kWebSocketErrorProtocolError;
    } else if (payload_length > static_cast<uint64_t>(INT32_MAX)) {
      websocket_error_ = kWebSocketErrorMessageTooBig;
    }
  }
  if (websocket_error_ != kWebSocketNormalClosure) {
    buffer_.clear();
    current_read_pos_ = 0;
    current_frame_header_.reset();
    frame_offset_ = 0;
    return;
  }

  if (masked) {
    if (end - current < kMaskingKeyLength)
      return;
    std::copy(current, current + kMaskingKeyLength, masking_key_.key);
    current += kMaskingKeyLength;
  } else {
    std::fill(masking_key_.key, masking_key_.key + kMaskingKeyLength, '\0');
  }

  current_frame_header_.reset(new WebSocketFrameHeader(opcode));
  current_frame_header_->final = final;
  current_frame_header_->reserved1 = reserved1;
  current_frame_header_->reserved2 = reserved2;
  current_frame_header_->reserved3 = reserved3;
  current_frame_header_->masked = masked;
  current_frame_header_->payload_length = payload_length;
  current_read_pos_ += current - start;
  DCHECK_EQ(0u, frame_offset_);
}

std::unique_ptr<WebSocketFrameChunk> WebSocketFrameParser::DecodeFramePayload(
    bool first_chunk) {
  // The cast here is safe because |payload_length| is already checked to be
  // less than std::numeric_limits<int>::max() when the header is parsed.
  int next_size = static_cast<int>(
      std::min(static_cast<uint64_t>(buffer_.size() - current_read_pos_),
               current_frame_header_->payload_length - frame_offset_));

  std::unique_ptr<WebSocketFrameChunk> frame_chunk(new WebSocketFrameChunk);
  if (first_chunk) {
    frame_chunk->header = current_frame_header_->Clone();
  }
  frame_chunk->final_chunk = false;
  if (next_size) {
    frame_chunk->data = new IOBufferWithSize(static_cast<int>(next_size));
    char* io_data = frame_chunk->data->data();
    memcpy(io_data, &buffer_.front() + current_read_pos_, next_size);
    if (current_frame_header_->masked) {
      // The masking function is its own inverse, so we use the same function to
      // unmask as to mask.
      MaskWebSocketFramePayload(
          masking_key_, frame_offset_, io_data, next_size);
    }

    current_read_pos_ += next_size;
    frame_offset_ += next_size;
  }

  DCHECK_LE(frame_offset_, current_frame_header_->payload_length);
  if (frame_offset_ == current_frame_header_->payload_length) {
    frame_chunk->final_chunk = true;
    current_frame_header_.reset();
    frame_offset_ = 0;
  }

  return frame_chunk;
}

}  // namespace net
