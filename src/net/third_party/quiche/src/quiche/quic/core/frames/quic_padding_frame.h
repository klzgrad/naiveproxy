// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_PADDING_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_PADDING_FRAME_H_

#include <cstdint>
#include <ostream>

#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A padding frame contains no payload.
struct QUICHE_EXPORT QuicPaddingFrame
    : public QuicInlinedFrame<QuicPaddingFrame> {
  QuicPaddingFrame() : QuicInlinedFrame(PADDING_FRAME) {}
  explicit QuicPaddingFrame(int num_padding_bytes)
      : QuicInlinedFrame(PADDING_FRAME), num_padding_bytes(num_padding_bytes) {}

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicPaddingFrame& padding_frame);

  QuicFrameType type;

  // -1: full padding to the end of a max-sized packet
  // otherwise: only pad up to num_padding_bytes bytes
  int num_padding_bytes = -1;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_PADDING_FRAME_H_
