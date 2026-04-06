// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_NEW_TOKEN_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_NEW_TOKEN_FRAME_H_

#include <memory>
#include <ostream>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

struct QUICHE_EXPORT QuicNewTokenFrame {
  QuicNewTokenFrame() = default;
  QuicNewTokenFrame(QuicControlFrameId control_frame_id,
                    absl::string_view token);

  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const QuicNewTokenFrame& s);

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;

  std::string token;
};
static_assert(sizeof(QuicNewTokenFrame) <= 64,
              "Keep the QuicNewTokenFrame size to a cacheline.");

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_NEW_TOKEN_FRAME_H_
