// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream_body_buffer.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

QuicSpdyStreamBodyBuffer::QuicSpdyStreamBodyBuffer()
    : bytes_remaining_(0),
      total_body_bytes_readable_(0),
      total_body_bytes_received_(0),
      total_payload_lengths_(0) {}

void QuicSpdyStreamBodyBuffer::OnDataHeader(Http3FrameLengths frame_lengths) {
  frame_meta_.push_back(frame_lengths);
  total_payload_lengths_ += frame_lengths.payload_length;
}

void QuicSpdyStreamBodyBuffer::OnDataPayload(QuicStringPiece payload) {
  DCHECK(!payload.empty());
  bodies_.push_back(payload);
  total_body_bytes_received_ += payload.length();
  total_body_bytes_readable_ += payload.length();
  DCHECK_LE(total_body_bytes_received_, total_payload_lengths_);
}

size_t QuicSpdyStreamBodyBuffer::OnBodyConsumed(size_t num_bytes) {
  // Check if the stream has enough decoded data.
  if (num_bytes > total_body_bytes_readable_) {
    QUIC_BUG << "Invalid argument to OnBodyConsumed."
             << " expect to consume: " << num_bytes
             << ", but not enough bytes available. "
             << "Total bytes readable are: " << total_body_bytes_readable_;
    return 0;
  }
  // Discard references in the stream before the sequencer marks them consumed.
  size_t remaining = num_bytes;
  while (remaining > 0) {
    if (bodies_.empty()) {
      QUIC_BUG << "Failed to consume because body buffer is empty.";
      return 0;
    }
    auto body = bodies_.front();
    bodies_.pop_front();
    if (body.length() <= remaining) {
      remaining -= body.length();
    } else {
      body = body.substr(remaining, body.length() - remaining);
      bodies_.push_front(body);
      remaining = 0;
    }
  }

  // Consume headers.
  size_t bytes_to_consume = 0;
  while (bytes_remaining_ < num_bytes) {
    if (frame_meta_.empty()) {
      QUIC_BUG << "Faild to consume because frame header buffer is empty.";
      return 0;
    }
    auto meta = frame_meta_.front();
    frame_meta_.pop_front();
    bytes_remaining_ += meta.payload_length;
    bytes_to_consume += meta.header_length;
  }
  bytes_to_consume += num_bytes;

  // Update accountings.
  bytes_remaining_ -= num_bytes;
  total_body_bytes_readable_ -= num_bytes;

  return bytes_to_consume;
}

int QuicSpdyStreamBodyBuffer::PeekBody(iovec* iov, size_t iov_len) const {
  DCHECK(iov != nullptr);
  DCHECK_GT(iov_len, 0u);

  if (bodies_.empty()) {
    iov[0].iov_base = nullptr;
    iov[0].iov_len = 0;
    return 0;
  }
  // Fill iovs with references from the stream.
  size_t iov_filled = 0;
  while (iov_filled < bodies_.size() && iov_filled < iov_len) {
    QuicStringPiece body = bodies_[iov_filled];
    iov[iov_filled].iov_base = const_cast<char*>(body.data());
    iov[iov_filled].iov_len = body.size();
    iov_filled++;
  }
  return iov_filled;
}

size_t QuicSpdyStreamBodyBuffer::ReadBody(const struct iovec* iov,
                                          size_t iov_len,
                                          size_t* total_bytes_read) {
  *total_bytes_read = 0;
  QuicByteCount total_remaining = total_body_bytes_readable_;
  size_t index = 0;
  size_t src_offset = 0;
  for (size_t i = 0; i < iov_len && total_remaining > 0; ++i) {
    char* dest = reinterpret_cast<char*>(iov[i].iov_base);
    size_t dest_remaining = iov[i].iov_len;
    while (dest_remaining > 0 && total_remaining > 0) {
      auto body = bodies_[index];
      size_t bytes_to_copy =
          std::min<size_t>(body.length() - src_offset, dest_remaining);
      memcpy(dest, body.substr(src_offset, bytes_to_copy).data(),
             bytes_to_copy);
      dest += bytes_to_copy;
      dest_remaining -= bytes_to_copy;
      *total_bytes_read += bytes_to_copy;
      total_remaining -= bytes_to_copy;
      if (bytes_to_copy < body.length() - src_offset) {
        src_offset += bytes_to_copy;
      } else {
        index++;
        src_offset = 0;
      }
    }
  }

  return OnBodyConsumed(*total_bytes_read);
}

}  // namespace quic
