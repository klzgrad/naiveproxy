// Copyright 2023 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/tools/naive/naive_padding_framer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

#include "base/check.h"
#include "base/check_op.h"

namespace net {
NaivePaddingFramer::NaivePaddingFramer(std::optional<int> max_read_frames)
    : max_read_frames_(max_read_frames) {
  if (max_read_frames.has_value()) {
    CHECK_GE(*max_read_frames, 0);
  }
}

int NaivePaddingFramer::Read(const char* padded,
                             int padded_len,
                             char* payload_buf,
                             int payload_buf_capacity) {
  // This check guarantees write_ptr does not overflow.
  CHECK_GE(payload_buf_capacity, padded_len);

  char* write_ptr = payload_buf;
  while (padded_len > 0) {
    int copy_size;
    switch (state_) {
      case ReadState::kPayloadLength1:
        if (max_read_frames_.has_value() &&
            num_read_frames_ >= *max_read_frames_) {
          std::memcpy(write_ptr, padded, padded_len);
          padded += padded_len;
          write_ptr += padded_len;
          padded_len = 0;
          break;
        }
        read_payload_length_ = static_cast<uint8_t>(padded[0]);
        ++padded;
        --padded_len;
        state_ = ReadState::kPayloadLength2;
        break;
      case ReadState::kPayloadLength2:
        read_payload_length_ =
            read_payload_length_ * 256 + static_cast<uint8_t>(padded[0]);
        ++padded;
        --padded_len;
        state_ = ReadState::kPaddingLength1;
        break;
      case ReadState::kPaddingLength1:
        read_padding_length_ = static_cast<uint8_t>(padded[0]);
        ++padded;
        --padded_len;
        state_ = ReadState::kPayload;
        break;
      case ReadState::kPayload:
        copy_size = std::min(read_payload_length_, padded_len);
        read_payload_length_ -= copy_size;
        if (read_payload_length_ == 0) {
          state_ = ReadState::kPadding;
        }

        std::memcpy(write_ptr, padded, copy_size);
        padded += copy_size;
        write_ptr += copy_size;
        padded_len -= copy_size;
        break;
      case ReadState::kPadding:
        copy_size = std::min(read_padding_length_, padded_len);
        read_padding_length_ -= copy_size;
        if (read_padding_length_ == 0) {
          if (num_read_frames_ < std::numeric_limits<int>::max() - 1) {
            ++num_read_frames_;
          }
          state_ = ReadState::kPayloadLength1;
        }

        padded += copy_size;
        padded_len -= copy_size;
        break;
    }
  }
  return write_ptr - payload_buf;
}

int NaivePaddingFramer::Write(const char* payload_buf,
                              int payload_buf_len,
                              int padding_size,
                              char* padded,
                              int padded_capacity,
                              int& payload_consumed_len) {
  CHECK_GE(payload_buf_len, 0);
  CHECK_LE(padding_size, max_padding_size());
  CHECK_GE(padding_size, 0);

  payload_consumed_len = std::min(
      payload_buf_len, padded_capacity - frame_header_size() - padding_size);
  int padded_buf_len =
      frame_header_size() + payload_consumed_len + padding_size;

  padded[0] = payload_consumed_len / 256;
  padded[1] = payload_consumed_len % 256;
  padded[2] = padding_size;
  std::memcpy(padded + frame_header_size(), payload_buf, payload_consumed_len);
  std::memset(padded + frame_header_size() + payload_consumed_len, '\0',
              padding_size);

  if (num_written_frames_ < std::numeric_limits<int>::max() - 1) {
    ++num_written_frames_;
  }
  return padded_buf_len;
}
}  // namespace net
