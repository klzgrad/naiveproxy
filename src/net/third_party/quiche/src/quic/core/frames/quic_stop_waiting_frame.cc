// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_stop_waiting_frame.h"

#include "net/third_party/quiche/src/quic/core/quic_constants.h"

namespace quic {

QuicStopWaitingFrame::QuicStopWaitingFrame()
    : QuicInlinedFrame(STOP_WAITING_FRAME) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicStopWaitingFrame& sent_info) {
  os << "{ least_unacked: " << sent_info.least_unacked << " }\n";
  return os;
}

}  // namespace quic
