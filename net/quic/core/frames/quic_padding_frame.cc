// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/frames/quic_padding_frame.h"

using std::string;

namespace net {

std::ostream& operator<<(std::ostream& os,
                         const QuicPaddingFrame& padding_frame) {
  os << "{ num_padding_bytes: " << padding_frame.num_padding_bytes << " }\n";
  return os;
}

}  // namespace net
