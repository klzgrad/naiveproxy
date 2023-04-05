// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TEST_TOOLS_FRAME_DECODER_STATE_TEST_UTIL_H_
#define QUICHE_HTTP2_TEST_TOOLS_FRAME_DECODER_STATE_TEST_UTIL_H_

#include "quiche/http2/decoder/frame_decoder_state.h"
#include "quiche/http2/http2_structures.h"
#include "quiche/http2/test_tools/random_decoder_test_base.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {

class QUICHE_NO_EXPORT FrameDecoderStatePeer {
 public:
  // Randomizes (i.e. corrupts) the fields of the FrameDecoderState.
  // PayloadDecoderBaseTest::StartDecoding calls this before passing the first
  // decode buffer to the payload decoder, which increases the likelihood of
  // detecting any use of prior states of the decoder on the decoding of
  // future payloads.
  static void Randomize(FrameDecoderState* p, Http2Random* rng);

  // Inject a frame header into the FrameDecoderState.
  // PayloadDecoderBaseTest::StartDecoding calls this just after calling
  // Randomize (above), to simulate a full frame decoder having just finished
  // decoding the common frame header and then calling the appropriate payload
  // decoder based on the frame type in that frame header.
  static void set_frame_header(const Http2FrameHeader& header,
                               FrameDecoderState* p);
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TEST_TOOLS_FRAME_DECODER_STATE_TEST_UTIL_H_
