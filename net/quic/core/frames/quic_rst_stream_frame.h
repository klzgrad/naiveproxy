// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_RST_STREAM_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_RST_STREAM_FRAME_H_

#include <ostream>

#include "net/quic/core/frames/quic_control_frame.h"
#include "net/quic/core/quic_error_codes.h"

namespace net {

struct QUIC_EXPORT_PRIVATE QuicRstStreamFrame : public QuicControlFrame {
  QuicRstStreamFrame();
  QuicRstStreamFrame(QuicControlFrameId control_frame_id,
                     QuicStreamId stream_id,
                     QuicRstStreamErrorCode error_code,
                     QuicStreamOffset bytes_written);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicRstStreamFrame& r);

  QuicStreamId stream_id;
  QuicRstStreamErrorCode error_code;

  // Used to update flow control windows. On termination of a stream, both
  // endpoints must inform the peer of the number of bytes they have sent on
  // that stream. This can be done through normal termination (data packet with
  // FIN) or through a RST.
  QuicStreamOffset byte_offset;
};

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_RST_STREAM_FRAME_H_
