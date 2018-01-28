// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"

namespace net {

bool QuicHttpFrameDecoderNoOpListener::OnFrameHeader(
    const QuicHttpFrameHeader& header) {
  return true;
}

}  // namespace net
