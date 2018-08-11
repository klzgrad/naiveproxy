// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_STATE_TEST_UTIL_H_
#define NET_THIRD_PARTY_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_STATE_TEST_UTIL_H_

#include "net/third_party/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/third_party/quic/http/quic_http_structures.h"
#include "net/third_party/quic/http/tools/quic_http_random_decoder_test.h"

namespace net {
namespace test {

class QuicHttpFrameDecoderStatePeer {
 public:
  // Inject a frame header into the QuicHttpFrameDecoderState.
  // QuicHttpPayloadDecoderBaseTest::StartDecoding calls this just after calling
  // Randomize (above), to simulate a full frame decoder having just finished
  // decoding the common frame header and then calling the appropriate payload
  // decoder based on the frame type in that frame header.
  static void set_frame_header(const QuicHttpFrameHeader& header,
                               QuicHttpFrameDecoderState* p);
};

}  // namespace test
}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_STATE_TEST_UTIL_H_
