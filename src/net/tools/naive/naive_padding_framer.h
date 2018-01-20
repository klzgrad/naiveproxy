// Copyright 2023 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_PADDING_FRAMER_H_
#define NET_TOOLS_NAIVE_NAIVE_PADDING_FRAMER_H_

#include <cstdint>
#include <limits>
#include <optional>

namespace net {

// struct PaddedFrame {
//   uint16_t payload_size;  // big-endian
//   uint8_t padding_size;  // big-endian
//   uint8_t payload[payload_size];
//   uint8_t zeros[padding_size];
// };
class NaivePaddingFramer {
 public:
  // `max_read_frames`: Assumes the byte stream stops using the padding
  //   framing after `max_read_frames` frames. If -1, it means
  //   the byte stream always uses the padding framing.
  explicit NaivePaddingFramer(std::optional<int> max_read_frames);

  int max_payload_size() const { return std::numeric_limits<uint16_t>::max(); }

  int max_padding_size() const { return std::numeric_limits<uint8_t>::max(); }

  int frame_header_size() const { return 3; }

  int num_read_frames() const { return num_read_frames_; }

  int num_written_frames() const { return num_written_frames_; }

  // Reads `padded` for `padded_len` bytes and extracts unpadded payload to
  // `payload_buf`.
  // Returns the number of payload bytes extracted.
  // Returning zero indicates a pure padding instead of EOF.
  int Read(const char* padded,
           int padded_len,
           char* payload_buf,
           int payload_buf_capacity);

  // Writes `payload_buf` for up to `payload_buf_len` bytes into `padded`.
  // Returns the number of padded bytes written.
  // If the padded bytes would exceed `padded_capacity`, the payload is
  // truncated to `payload_consumed_len`.
  int Write(const char* payload_buf,
            int payload_buf_len,
            int padding_size,
            char* padded,
            int padded_capacity,
            int& payload_consumed_len);

 private:
  enum class ReadState {
    kPayloadLength1,
    kPayloadLength2,
    kPaddingLength1,
    kPayload,
    kPadding,
  };

  std::optional<int> max_read_frames_;

  ReadState state_ = ReadState::kPayloadLength1;
  int read_payload_length_ = 0;
  int read_padding_length_ = 0;
  int num_read_frames_ = 0;

  int num_written_frames_ = 0;
};
}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_PADDING_FRAMER_H_
