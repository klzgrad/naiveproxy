// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_

#include <memory>
#include <ostream>

#include "net/third_party/quiche/src/quic/core/frames/quic_inlined_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicStreamFrame
    : public QuicInlinedFrame<QuicStreamFrame> {
  QuicStreamFrame();
  QuicStreamFrame(QuicStreamId stream_id,
                  bool fin,
                  QuicStreamOffset offset,
                  quiche::QuicheStringPiece data);
  QuicStreamFrame(QuicStreamId stream_id,
                  bool fin,
                  QuicStreamOffset offset,
                  QuicPacketLength data_length);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const QuicStreamFrame& s);

  bool operator==(const QuicStreamFrame& rhs) const;

  bool operator!=(const QuicStreamFrame& rhs) const;

  bool fin;
  QuicPacketLength data_length;
  QuicStreamId stream_id;
  const char* data_buffer;  // Not owned.
  QuicStreamOffset offset;  // Location of this data in the stream.

  QuicStreamFrame(QuicStreamId stream_id,
                  bool fin,
                  QuicStreamOffset offset,
                  const char* data_buffer,
                  QuicPacketLength data_length);
};
static_assert(sizeof(QuicStreamFrame) <= 64,
              "Keep the QuicStreamFrame size to a cacheline.");

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_
