// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/frames/quic_message_frame.h"

#include "net/third_party/quic/core/quic_constants.h"
#include "net/third_party/quic/platform/api/quic_logging.h"

namespace quic {

QuicMessageFrame::QuicMessageFrame() : message_id(0) {}

QuicMessageFrame::QuicMessageFrame(QuicMessageId message_id,
                                   QuicStringPiece message_data)
    : message_id(message_id), message_data(message_data) {}

QuicMessageFrame::~QuicMessageFrame() {}

std::ostream& operator<<(std::ostream& os, const QuicMessageFrame& s) {
  os << " message_id: " << s.message_id
     << ", message_length: " << s.message_data.length() << " }\n";
  return os;
}

}  // namespace quic
