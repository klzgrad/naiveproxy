// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_connection_close_frame.h"

#include <memory>

#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {

QuicConnectionCloseFrame::QuicConnectionCloseFrame()
    // Default close type ensures that existing, pre-V99 code works as expected.
    : close_type(GOOGLE_QUIC_CONNECTION_CLOSE),
      quic_error_code(QUIC_NO_ERROR),
      extracted_error_code(QUIC_IETF_GQUIC_ERROR_MISSING),
      transport_close_frame_type(0) {}

QuicConnectionCloseFrame::QuicConnectionCloseFrame(QuicErrorCode error_code,
                                                   std::string error_details)
    // Default close type ensures that existing, pre-V99 code works as expected.
    : close_type(GOOGLE_QUIC_CONNECTION_CLOSE),
      quic_error_code(error_code),
      extracted_error_code(error_code),
      error_details(std::move(error_details)),
      transport_close_frame_type(0) {}

QuicConnectionCloseFrame::QuicConnectionCloseFrame(
    QuicErrorCode quic_error_code,
    std::string error_details,
    uint64_t ietf_application_error_code)
    : close_type(IETF_QUIC_APPLICATION_CONNECTION_CLOSE),
      application_error_code(ietf_application_error_code),
      extracted_error_code(quic_error_code),
      error_details(std::move(error_details)),
      transport_close_frame_type(0) {}

QuicConnectionCloseFrame::QuicConnectionCloseFrame(
    QuicErrorCode quic_error_code,
    std::string error_details,
    QuicIetfTransportErrorCodes transport_error_code,
    uint64_t transport_frame_type)
    : close_type(IETF_QUIC_TRANSPORT_CONNECTION_CLOSE),
      transport_error_code(transport_error_code),
      extracted_error_code(quic_error_code),
      error_details(std::move(error_details)),
      transport_close_frame_type(transport_frame_type) {}

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
