// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/http/decoder/quic_http_frame_decoder_state_test_util.h"

#include "base/logging.h"
#include "net/third_party/quic/http/decoder/quic_http_structure_decoder_test_util.h"
#include "net/third_party/quic/http/quic_http_structures.h"
#include "net/third_party/quic/http/quic_http_structures_test_util.h"
#include "net/third_party/quic/http/tools/quic_http_random_decoder_test.h"

namespace net {
namespace test {

// static
void QuicHttpFrameDecoderStatePeer::set_frame_header(
    const QuicHttpFrameHeader& header,
    QuicHttpFrameDecoderState* p) {
  VLOG(1) << "QuicHttpFrameDecoderStatePeer::set_frame_header " << header;
  p->frame_header_ = header;
}

}  // namespace test
}  // namespace net
