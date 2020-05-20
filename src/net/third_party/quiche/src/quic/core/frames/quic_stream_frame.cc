// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_stream_frame.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QuicStreamFrame::QuicStreamFrame()
    : QuicStreamFrame(-1, false, 0, nullptr, 0) {}

QuicStreamFrame::QuicStreamFrame(QuicStreamId stream_id,
                                 bool fin,
                                 QuicStreamOffset offset,
                                 quiche::QuicheStringPiece data)
    : QuicStreamFrame(stream_id, fin, offset, data.data(), data.length()) {}

QuicStreamFrame::QuicStreamFrame(QuicStreamId stream_id,
                                 bool fin,
                                 QuicStreamOffset offset,
                                 QuicPacketLength data_length)
    : QuicStreamFrame(stream_id, fin, offset, nullptr, data_length) {}

QuicStreamFrame::QuicStreamFrame(QuicStreamId stream_id,
                                 bool fin,
                                 QuicStreamOffset offset,
                                 const char* data_buffer,
                                 QuicPacketLength data_length)
    : QuicInlinedFrame(STREAM_FRAME),
      fin(fin),
      data_length(data_length),
      stream_id(stream_id),
      data_buffer(data_buffer),
      offset(offset) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicStreamFrame& stream_frame) {
  os << "{ stream_id: " << stream_frame.stream_id
     << ", fin: " << stream_frame.fin << ", offset: " << stream_frame.offset
     << ", length: " << stream_frame.data_length << " }\n";
  return os;
}

bool QuicStreamFrame::operator==(const QuicStreamFrame& rhs) const {
  return fin == rhs.fin && data_length == rhs.data_length &&
         stream_id == rhs.stream_id && data_buffer == rhs.data_buffer &&
         offset == rhs.offset;
}

bool QuicStreamFrame::operator!=(const QuicStreamFrame& rhs) const {
  return !(*this == rhs);
}

}  // namespace quic
