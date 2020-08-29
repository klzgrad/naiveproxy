// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/continuation_payload_decoder.h"

#include <stddef.h>

#include <string>
#include <type_traits>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/payload_decoder_base_test_util.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts_collector.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"

namespace http2 {
namespace test {

// Provides friend access to an instance of the payload decoder, and also
// provides info to aid in testing.
class ContinuationPayloadDecoderPeer {
 public:
  static constexpr Http2FrameType FrameType() {
    return Http2FrameType::CONTINUATION;
  }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() { return 0; }
};

namespace {

struct Listener : public FramePartsCollector {
  void OnContinuationStart(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnContinuationStart: " << header;
    StartFrame(header)->OnContinuationStart(header);
  }

  void OnHpackFragment(const char* data, size_t len) override {
    HTTP2_VLOG(1) << "OnHpackFragment: len=" << len;
    CurrentFrame()->OnHpackFragment(data, len);
  }

  void OnContinuationEnd() override {
    HTTP2_VLOG(1) << "OnContinuationEnd";
    EndFrame()->OnContinuationEnd();
  }
};

class ContinuationPayloadDecoderTest
    : public AbstractPayloadDecoderTest<ContinuationPayloadDecoder,
                                        ContinuationPayloadDecoderPeer,
                                        Listener>,
      public ::testing::WithParamInterface<uint32_t> {
 protected:
  ContinuationPayloadDecoderTest() : length_(GetParam()) {
    HTTP2_VLOG(1) << "################  length_=" << length_
                  << "  ################";
  }

  const uint32_t length_;
};

INSTANTIATE_TEST_SUITE_P(VariousLengths,
                         ContinuationPayloadDecoderTest,
                         ::testing::Values(0, 1, 2, 3, 4, 5, 6));

TEST_P(ContinuationPayloadDecoderTest, ValidLength) {
  std::string hpack_payload = Random().RandString(length_);
  Http2FrameHeader frame_header(length_, Http2FrameType::CONTINUATION,
                                RandFlags(), RandStreamId());
  set_frame_header(frame_header);
  FrameParts expected(frame_header, hpack_payload);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(hpack_payload, expected));
}

}  // namespace
}  // namespace test
}  // namespace http2
