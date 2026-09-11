// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_path_response_frame.h"

#include <ostream>

#include "absl/strings/escaping.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

QuicPathResponseFrame::QuicPathResponseFrame()
    : QuicInlinedFrame(PATH_RESPONSE_FRAME) {}

QuicPathResponseFrame::QuicPathResponseFrame(
    QuicControlFrameId control_frame_id, const QuicPathFrameBuffer& data_buff)
    : QuicInlinedFrame(PATH_RESPONSE_FRAME),
      control_frame_id(control_frame_id) {
  memcpy(data_buffer.data(), data_buff.data(), data_buffer.size());
}

std::ostream& operator<<(std::ostream& os, const QuicPathResponseFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id << ", data: "
     << absl::BytesToHexString(absl::string_view(
            reinterpret_cast<const char*>(frame.data_buffer.data()),
            frame.data_buffer.size()))
     << " }\n";
  return os;
}

}  // namespace quic
