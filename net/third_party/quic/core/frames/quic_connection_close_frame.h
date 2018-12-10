// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_
#define NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_

#include <ostream>

#include "net/third_party/quic/core/quic_error_codes.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicConnectionCloseFrame {
  QuicConnectionCloseFrame();
  QuicConnectionCloseFrame(QuicErrorCode error_code, QuicString error_details);
  QuicConnectionCloseFrame(QuicIetfTransportErrorCodes ietf_error_code,
                           QuicString error_details);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicConnectionCloseFrame& c);

  // Set error_code or ietf_error_code based on the transport version
  // currently in use.
  union {
    // IETF QUIC has a different set of error codes. Include both
    // code-sets.
    QuicErrorCode error_code;
    QuicIetfTransportErrorCodes ietf_error_code;
  };
  QuicString error_details;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_
