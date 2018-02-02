// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_frame_decoder_state_test_util.h"

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_structure_decoder_test_util.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/http/quic_http_structures_test_util.h"
#include "net/quic/http/tools/quic_http_random_decoder_test.h"

namespace net {
namespace test {

// static
void QuicHttpFrameDecoderStatePeer::Randomize(QuicHttpFrameDecoderState* p,
                                              QuicTestRandomBase* rng) {
  VLOG(1) << "QuicHttpFrameDecoderStatePeer::Randomize";
  ::net::test::Randomize(&p->frame_header_, rng);
  p->remaining_payload_ = rng->Rand32();
  p->remaining_padding_ = rng->Rand32();
  QuicHttpStructureDecoderPeer::Randomize(&p->structure_decoder_, rng);
}

// static
void QuicHttpFrameDecoderStatePeer::set_frame_header(
    const QuicHttpFrameHeader& header,
    QuicHttpFrameDecoderState* p) {
  VLOG(1) << "QuicHttpFrameDecoderStatePeer::set_frame_header " << header;
  p->frame_header_ = header;
}

}  // namespace test
}  // namespace net
