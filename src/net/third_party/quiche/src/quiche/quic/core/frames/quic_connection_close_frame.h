// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_

#include <ostream>
#include <string>

#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

struct QUICHE_EXPORT QuicConnectionCloseFrame {
  QuicConnectionCloseFrame() = default;

  // Builds a connection close frame based on the transport version
  // and the mapping of error_code. THIS IS THE PREFERRED C'TOR
  // TO USE IF YOU NEED TO CREATE A CONNECTION-CLOSE-FRAME AND
  // HAVE IT BE CORRECT FOR THE VERSION AND CODE MAPPINGS.
  // |ietf_error| may optionally be be used to directly specify the wire
  // error code. Otherwise if |ietf_error| is NO_IETF_QUIC_ERROR, the
  // QuicErrorCodeToTransportErrorCode mapping of |error_code| will be used.
  QuicConnectionCloseFrame(QuicTransportVersion transport_version,
                           QuicErrorCode error_code,
                           QuicIetfTransportErrorCodes ietf_error,
                           std::string error_phrase,
                           uint64_t transport_close_frame_type);

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicConnectionCloseFrame& c);

  // Indicates whether the the frame is a Google QUIC CONNECTION_CLOSE frame,
  // an IETF QUIC CONNECTION_CLOSE frame with transport error code,
  // or an IETF QUIC CONNECTION_CLOSE frame with application error code.
  QuicConnectionCloseType close_type = GOOGLE_QUIC_CONNECTION_CLOSE;

  // The error code on the wire.  For Google QUIC frames, this has the same
  // value as |quic_error_code|.
  uint64_t wire_error_code = QUIC_NO_ERROR;

  // The underlying error.  For Google QUIC frames, this has the same value as
  // |wire_error_code|.  For sent IETF QUIC frames, this is the error that
  // triggered the closure of the connection.  For received IETF QUIC frames,
  // this is parsed from the Reason Phrase field of the CONNECTION_CLOSE frame,
  // or QUIC_IETF_GQUIC_ERROR_MISSING.
  QuicErrorCode quic_error_code = QUIC_NO_ERROR;

  // String with additional error details. |quic_error_code| and a colon will be
  // prepended to the error details when sending IETF QUIC frames, and parsed
  // into |quic_error_code| upon receipt, when present.
  std::string error_details;

  // The frame type present in the IETF transport connection close frame.
  // Not populated for the Google QUIC or application connection close frames.
  // Contains the type of frame that triggered the connection close. Made a
  // uint64, as opposed to the QuicIetfFrameType, to support possible
  // extensions as well as reporting invalid frame types received from the peer.
  uint64_t transport_close_frame_type = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_
