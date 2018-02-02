// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_PADDING_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_PADDING_FRAME_H_

#include <cstdint>
#include <ostream>

#include "net/quic/platform/api/quic_export.h"

namespace net {

// A padding frame contains no payload.
struct QUIC_EXPORT_PRIVATE QuicPaddingFrame {
  QuicPaddingFrame() : num_padding_bytes(-1) {}
  explicit QuicPaddingFrame(int num_padding_bytes)
      : num_padding_bytes(num_padding_bytes) {}

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicPaddingFrame& s);

  // -1: full padding to the end of a max-sized packet
  // otherwise: only pad up to num_padding_bytes bytes
  int num_padding_bytes;
};

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_PADDING_FRAME_H_
