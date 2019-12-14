// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_connection_close_frame.h"

#include <memory>

#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {
QuicConnectionCloseFrame::QuicConnectionCloseFrame()
    // Default close type ensures that existing, pre-V99 code works as expected.
    : close_type(GOOGLE_QUIC_CONNECTION_CLOSE),
      quic_error_code(QUIC_NO_ERROR),
      extracted_error_code(QUIC_NO_ERROR),
      transport_close_frame_type(0) {}

QuicConnectionCloseFrame::QuicConnectionCloseFrame(
    QuicTransportVersion transport_version,
    QuicErrorCode error_code,
    std::string error_phrase,
    uint64_t frame_type)
    : extracted_error_code(error_code), error_details(error_phrase) {
  if (!VersionHasIetfQuicFrames(transport_version)) {
    close_type = GOOGLE_QUIC_CONNECTION_CLOSE;
    quic_error_code = error_code;
    transport_close_frame_type = 0;
    return;
  }
  QuicErrorCodeToIetfMapping mapping =
      QuicErrorCodeToTransportErrorCode(error_code);
  if (mapping.is_transport_close_) {
    // Maps to a transport close
    close_type = IETF_QUIC_TRANSPORT_CONNECTION_CLOSE;
    transport_error_code = mapping.transport_error_code_;
    transport_close_frame_type = frame_type;
    return;
  }
  // Maps to an application close.
  close_type = IETF_QUIC_APPLICATION_CONNECTION_CLOSE;
  application_error_code = mapping.application_error_code_;
  transport_close_frame_type = 0;
}

std::ostream& operator<<(
    std::ostream& os,
    const QuicConnectionCloseFrame& connection_close_frame) {
  os << "{ Close type: " << connection_close_frame.close_type
     << ", error_code: ";
  switch (connection_close_frame.close_type) {
    case IETF_QUIC_TRANSPORT_CONNECTION_CLOSE:
      os << connection_close_frame.transport_error_code;
      break;
    case IETF_QUIC_APPLICATION_CONNECTION_CLOSE:
      os << connection_close_frame.application_error_code;
      break;
    case GOOGLE_QUIC_CONNECTION_CLOSE:
      os << connection_close_frame.quic_error_code;
      break;
  }
  os << ", extracted_error_code: "
     << QuicErrorCodeToString(connection_close_frame.extracted_error_code)
     << ", error_details: '" << connection_close_frame.error_details << "'";
  if (connection_close_frame.close_type ==
      IETF_QUIC_TRANSPORT_CONNECTION_CLOSE) {
    os << ", frame_type: "
       << static_cast<QuicIetfFrameType>(
              connection_close_frame.transport_close_frame_type);
  }
  os << "}\n";
  return os;
}

}  // namespace quic
