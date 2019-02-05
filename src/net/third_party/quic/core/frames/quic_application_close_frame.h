// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_APPLICATION_CLOSE_FRAME_H_
#define NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_APPLICATION_CLOSE_FRAME_H_

#include <ostream>

#include "net/third_party/quic/core/quic_error_codes.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicApplicationCloseFrame {
  QuicApplicationCloseFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicApplicationCloseFrame& frame);

  QuicErrorCode error_code;
  QuicString error_details;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_APPLICATION_CLOSE_FRAME_H_
