// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream_body_manager.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QuicSpdyStreamBodyManager::QuicSpdyStreamBodyManager()
    : total_body_bytes_received_(0) {}

size_t QuicSpdyStreamBodyManager::OnNonBody(QuicByteCount length) {
  DCHECK_NE(0u, length);

  if (fragments_.empty()) {
    // Non-body bytes can be consumed immediately, because all previously
    // received body bytes have been read.
    return length;
  }

  // Non-body bytes will be consumed after last body fragment is read.
  fragments_.back().trailing_non_body_byte_count += length;
  return 0;
}

void QuicSpdyStreamBodyManager::OnBody(quiche::QuicheStringPiece body) {
  DCHECK(!body.empty());

  fragments_.push_back({body, 0});
  total_body_bytes_received_ += body.length();
}

size_t QuicSpdyStreamBodyManager::OnBodyConsumed(size_t num_bytes) {
  QuicByteCount bytes_to_consume = 0;
  size_t remaining_bytes = num_bytes;

  while (remaining_bytes > 0) {
    if (fragments_.empty()) {
      QUIC_BUG << "Not enough available body to consume.";
      return 0;
    }

    Fragment& fragment = fragments_.front();
    const quiche::QuicheStringPiece body = fragment.body;

    if (body.length() > remaining_bytes) {
      // Consume leading |remaining_bytes| bytes of body.
      bytes_to_consume += remaining_bytes;
      fragment.body = body.substr(remaining_bytes);
      return bytes_to_consume;
    }

    // Consume entire fragment and the following
    // |trailing_non_body_byte_count| bytes.
    remaining_bytes -= body.length();
    bytes_to_consume += body.length() + fragment.trailing_non_body_byte_count;
    fragments_.pop_front();
  }

  return bytes_to_consume;
}

int QuicSpdyStreamBodyManager::PeekBody(iovec* iov, size_t iov_len) const {
  DCHECK(iov);
  DCHECK_GT(iov_len, 0u);

  // TODO(bnc): Is this really necessary?
  if (fragments_.empty()) {
    iov[0].iov_base = nullptr;
    iov[0].iov_len = 0;
    return 0;
  }

  size_t iov_filled = 0;
  while (iov_filled < fragments_.size() && iov_filled < iov_len) {
    quiche::QuicheStringPiece body = fragments_[iov_filled].body;
    iov[iov_filled].iov_base = const_cast<char*>(body.data());
    iov[iov_filled].iov_len = body.size();
    iov_filled++;
  }

  return iov_filled;
}

size_t QuicSpdyStreamBodyManager::ReadBody(const struct iovec* iov,
                                           size_t iov_len,
                                           size_t* total_bytes_read) {
  *total_bytes_read = 0;
  QuicByteCount bytes_to_consume = 0;

  // The index of iovec to write to.
  size_t index = 0;
  // Address to write to within current iovec.
  char* dest = reinterpret_cast<char*>(iov[index].iov_base);
  // Remaining space in current iovec.
  size_t dest_remaining = iov[index].iov_len;

  while (!fragments_.empty()) {
    Fragment& fragment = fragments_.front();
    const quiche::QuicheStringPiece body = fragment.body;

    const size_t bytes_to_copy =
        std::min<size_t>(body.length(), dest_remaining);
    memcpy(dest, body.data(), bytes_to_copy);
    bytes_to_consume += bytes_to_copy;
    *total_bytes_read += bytes_to_copy;

    if (bytes_to_copy == body.length()) {
      // Entire fragment read.
      bytes_to_consume += fragment.trailing_non_body_byte_count;
      fragments_.pop_front();
    } else {
      // Consume leading |bytes_to_copy| bytes of body.
      fragment.body = body.substr(bytes_to_copy);
    }

    if (bytes_to_copy == dest_remaining) {
      // Current iovec full.
      ++index;
      if (index == iov_len) {
        break;
      }
      dest = reinterpret_cast<char*>(iov[index].iov_base);
      dest_remaining = iov[index].iov_len;
    } else {
      // Advance destination parameters within this iovec.
      dest += bytes_to_copy;
      dest_remaining -= bytes_to_copy;
    }
  }

  return bytes_to_consume;
}

}  // namespace quic
