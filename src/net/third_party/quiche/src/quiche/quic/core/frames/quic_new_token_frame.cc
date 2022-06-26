// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_new_token_frame.h"

#include "absl/strings/escaping.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

QuicNewTokenFrame::QuicNewTokenFrame(QuicControlFrameId control_frame_id,
                                     absl::string_view token)
    : control_frame_id(control_frame_id),
      token(std::string(token.data(), token.length())) {}

std::ostream& operator<<(std::ostream& os, const QuicNewTokenFrame& s) {
  os << "{ control_frame_id: " << s.control_frame_id
     << ", token: " << absl::BytesToHexString(s.token) << " }\n";
  return os;
}

}  // namespace quic
