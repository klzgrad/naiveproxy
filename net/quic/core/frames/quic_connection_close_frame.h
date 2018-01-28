// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_

#include <ostream>
#include <string>

#include "net/quic/core/quic_error_codes.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

struct QUIC_EXPORT_PRIVATE QuicConnectionCloseFrame {
  QuicConnectionCloseFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicConnectionCloseFrame& c);

  QuicErrorCode error_code;
  std::string error_details;
};

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_CONNECTION_CLOSE_FRAME_H_
