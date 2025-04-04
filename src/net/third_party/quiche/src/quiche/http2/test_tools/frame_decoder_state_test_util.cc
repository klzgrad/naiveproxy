// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/test_tools/frame_decoder_state_test_util.h"

#include "quiche/http2/core/http2_structures.h"
#include "quiche/http2/test_tools/http2_random.h"
#include "quiche/http2/test_tools/http2_structure_decoder_test_util.h"
#include "quiche/http2/test_tools/http2_structures_test_util.h"
#include "quiche/http2/test_tools/random_decoder_test_base.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {
namespace test {

// static
void FrameDecoderStatePeer::Randomize(FrameDecoderState* p, Http2Random* rng) {
  QUICHE_VLOG(1) << "FrameDecoderStatePeer::Randomize";
  ::http2::test::Randomize(&p->frame_header_, rng);
  p->remaining_payload_ = rng->Rand32();
  p->remaining_padding_ = rng->Rand32();
  Http2StructureDecoderPeer::Randomize(&p->structure_decoder_, rng);
}

// static
void FrameDecoderStatePeer::set_frame_header(const Http2FrameHeader& header,
                                             FrameDecoderState* p) {
  QUICHE_VLOG(1) << "FrameDecoderStatePeer::set_frame_header " << header;
  p->frame_header_ = header;
}

}  // namespace test
}  // namespace http2
