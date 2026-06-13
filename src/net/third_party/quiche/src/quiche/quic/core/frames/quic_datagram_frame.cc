// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_datagram_frame.h"

#include <ostream>
#include <utility>

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/quiche_mem_slice.h"

namespace quic {

QuicDatagramFrame::QuicDatagramFrame(QuicDatagramId datagram_id)
    : datagram_id(datagram_id), data(nullptr), datagram_length(0) {}

QuicDatagramFrame::QuicDatagramFrame(QuicDatagramId datagram_id,
                                     absl::Span<quiche::QuicheMemSlice> span)
    : datagram_id(datagram_id), data(nullptr), datagram_length(0) {
  for (quiche::QuicheMemSlice& slice : span) {
    if (slice.empty()) {
      continue;
    }
    datagram_length += slice.length();
    datagram_data.push_back(std::move(slice));
  }
}
QuicDatagramFrame::QuicDatagramFrame(QuicDatagramId datagram_id,
                                     quiche::QuicheMemSlice slice)
    : QuicDatagramFrame(datagram_id, absl::MakeSpan(&slice, 1)) {}

QuicDatagramFrame::QuicDatagramFrame(const char* data, QuicPacketLength length)
    : datagram_id(0), data(data), datagram_length(length) {}

QuicDatagramFrame::~QuicDatagramFrame() {}

std::ostream& operator<<(std::ostream& os, const QuicDatagramFrame& s) {
  os << " datagram_id: " << s.datagram_id
     << ", datagram_length: " << s.datagram_length << " }\n";
  return os;
}

}  // namespace quic
