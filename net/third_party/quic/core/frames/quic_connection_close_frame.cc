// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/frames/quic_connection_close_frame.h"

namespace quic {

QuicConnectionCloseFrame::QuicConnectionCloseFrame()
    : error_code(QUIC_NO_ERROR) {}

QuicConnectionCloseFrame::QuicConnectionCloseFrame(QuicErrorCode error_code,
                                                   QuicString error_details)
    : error_code(error_code), error_details(error_details) {}

QuicConnectionCloseFrame::QuicConnectionCloseFrame(
    QuicIetfTransportErrorCodes ietf_error_code,
    QuicString error_details)
    : ietf_error_code(ietf_error_code),
      error_details(std::move(error_details)) {}

std::ostream& operator<<(
    std::ostream& os,
    const QuicConnectionCloseFrame& connection_close_frame) {
  os << "{ error_code: " << connection_close_frame.error_code
     << ", error_details: '" << connection_close_frame.error_details << "' }\n";
  return os;
}

}  // namespace quic
