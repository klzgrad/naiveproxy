// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_

#include <memory>
#include <ostream>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicStreamFrame
    : public QuicInlinedFrame<QuicStreamFrame> {
  QuicStreamFrame();
  QuicStreamFrame(QuicStreamId stream_id, bool fin, QuicStreamOffset offset,
                  absl::string_view data);
  QuicStreamFrame(QuicStreamId stream_id, bool fin, QuicStreamOffset offset,
                  QuicPacketLength data_length);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const QuicStreamFrame& s);

  bool operator==(const QuicStreamFrame& rhs) const;

  bool operator!=(const QuicStreamFrame& rhs) const;

  QuicFrameType type;
  bool fin = false;
  QuicPacketLength data_length = 0;
  // TODO(wub): Change to a QuicUtils::GetInvalidStreamId when it is not version
  // dependent.
  QuicStreamId stream_id = -1;
  const char* data_buffer = nullptr;  // Not owned.
  QuicStreamOffset offset = 0;        // Location of this data in the stream.

  QuicStreamFrame(QuicStreamId stream_id, bool fin, QuicStreamOffset offset,
                  const char* data_buffer, QuicPacketLength data_length);
};
static_assert(sizeof(QuicStreamFrame) <= 64,
              "Keep the QuicStreamFrame size to a cacheline.");

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_
