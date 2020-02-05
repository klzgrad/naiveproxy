// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_message_frame.h"

#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice.h"

namespace quic {

QuicMessageFrame::QuicMessageFrame()
    : message_id(0), data(nullptr), message_length(0) {}

QuicMessageFrame::QuicMessageFrame(QuicMessageId message_id)
    : message_id(message_id), data(nullptr), message_length(0) {}

QuicMessageFrame::QuicMessageFrame(QuicMessageId message_id,
                                   QuicMemSliceSpan span)
    : message_id(message_id), data(nullptr), message_length(0) {
  span.ConsumeAll([&](QuicMemSlice slice) {
    message_length += slice.length();
    message_data.push_back(std::move(slice));
  });
}

QuicMessageFrame::QuicMessageFrame(const char* data, QuicPacketLength length)
    : message_id(0), data(data), message_length(length) {}

QuicMessageFrame::~QuicMessageFrame() {}

std::ostream& operator<<(std::ostream& os, const QuicMessageFrame& s) {
  os << " message_id: " << s.message_id
     << ", message_length: " << s.message_length << " }\n";
  return os;
}

}  // namespace quic
