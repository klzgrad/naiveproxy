// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/decoder/http2_frame_decoder_listener.h"

namespace net {

bool Http2FrameDecoderNoOpListener::OnFrameHeader(
    const Http2FrameHeader& header) {
  return true;
}

}  // namespace net
