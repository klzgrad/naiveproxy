// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_connection_close_frame.h"

#include <memory>
#include <ostream>
#include <string>

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

QuicConnectionCloseFrame::QuicConnectionCloseFrame(
    QuicTransportVersion transport_version, QuicErrorCode error_code,
    QuicIetfTransportErrorCodes ietf_error, std::string error_phrase,
    uint64_t frame_type)
    : quic_error_code(error_code), error_details(error_phrase) {
  if (!VersionHasIetfQuicFrames(transport_version)) {
    close_type = GOOGLE_QUIC_CONNECTION_CLOSE;
    wire_error_code = error_code;
    transport_close_frame_type = 0;
    return;
  }
  QuicErrorCodeToIetfMapping mapping =
      QuicErrorCodeToTransportErrorCode(error_code);
  if (ietf_error != NO_IETF_QUIC_ERROR) {
    wire_error_code = ietf_error;
  } else {
    wire_error_code = mapping.error_code;
  }
  if (mapping.is_transport_close) {
    // Maps to a transport close
    close_type = IETF_QUIC_TRANSPORT_CONNECTION_CLOSE;
    transport_close_frame_type = frame_type;
    return;
  }
  // Maps to an application close.
  close_type = IETF_QUIC_APPLICATION_CONNECTION_CLOSE;
  transport_close_frame_type = 0;
}

std::ostream& operator<<(
    std::ostream& os, const QuicConnectionCloseFrame& connection_close_frame) {
  os << "{ Close type: " << connection_close_frame.close_type;
  switch (connection_close_frame.close_type) {
    case IETF_QUIC_TRANSPORT_CONNECTION_CLOSE:
      os << ", wire_error_code: "
         << static_cast<QuicIetfTransportErrorCodes>(
                connection_close_frame.wire_error_code);
      break;
    case IETF_QUIC_APPLICATION_CONNECTION_CLOSE:
      os << ", wire_error_code: " << connection_close_frame.wire_error_code;
      break;
    case GOOGLE_QUIC_CONNECTION_CLOSE:
      // Do not log, value same as |quic_error_code|.
      break;
  }
  os << ", quic_error_code: "
     << QuicErrorCodeToString(connection_close_frame.quic_error_code)
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
