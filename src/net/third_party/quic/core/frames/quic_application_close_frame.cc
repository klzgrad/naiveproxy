// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/frames/quic_application_close_frame.h"

namespace quic {

QuicApplicationCloseFrame::QuicApplicationCloseFrame()
    : error_code(QUIC_NO_ERROR) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicApplicationCloseFrame& frame) {
  os << "{ error_code: " << frame.error_code << ", error_details: '"
     << frame.error_details << "' }\n";
  return os;
}

}  // namespace quic
