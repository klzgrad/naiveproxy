// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/goaway_payload_decoder.h"

#include <stddef.h>

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/payload_decoder_base_test_util.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures_test_util.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts_collector.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/http2_frame_builder.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"

namespace http2 {
namespace test {

class GoAwayPayloadDecoderPeer {
 public:
  static constexpr Http2FrameType FrameType() { return Http2FrameType::GOAWAY; }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() { return 0; }
};

namespace {

struct Listener : public FramePartsCollector {
  void OnGoAwayStart(const Http2FrameHeader& header,
                     const Http2GoAwayFields& goaway) override {
    HTTP2_VLOG(1) << "OnGoAwayStart header: " << header
                  << "; goaway: " << goaway;
    StartFrame(header)->OnGoAwayStart(header, goaway);
  }

  void OnGoAwayOpaqueData(const char* data, size_t len) override {
    HTTP2_VLOG(1) << "OnGoAwayOpaqueData: len=" << len;
    CurrentFrame()->OnGoAwayOpaqueData(data, len);
  }

  void OnGoAwayEnd() override {
    HTTP2_VLOG(1) << "OnGoAwayEnd";
    EndFrame()->OnGoAwayEnd();
  }

  void OnFrameSizeError(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
    FrameError(header)->OnFrameSizeError(header);
  }
};

class GoAwayPayloadDecoderTest
    : public AbstractPayloadDecoderTest<GoAwayPayloadDecoder,
                                        GoAwayPayloadDecoderPeer,
                                        Listener> {};

// Confirm we get an error if the payload is not long enough to hold
// Http2GoAwayFields.
TEST_F(GoAwayPayloadDecoderTest, Truncated) {
  auto approve_size = [](size_t size) {
    return size != Http2GoAwayFields::EncodedSize();
  };
  Http2FrameBuilder fb;
  fb.Append(Http2GoAwayFields(123, Http2ErrorCode::ENHANCE_YOUR_CALM));
  EXPECT_TRUE(VerifyDetectsFrameSizeError(0, fb.buffer(), approve_size));
}

class GoAwayOpaqueDataLengthTests
    : public GoAwayPayloadDecoderTest,
      public ::testing::WithParamInterface<uint32_t> {
 protected:
  GoAwayOpaqueDataLengthTests() : length_(GetParam()) {
    HTTP2_VLOG(1) << "################  length_=" << length_
                  << "  ################";
  }

  const uint32_t length_;
};

INSTANTIATE_TEST_SUITE_P(VariousLengths,
                         GoAwayOpaqueDataLengthTests,
                         ::testing::Values(0, 1, 2, 3, 4, 5, 6));

TEST_P(GoAwayOpaqueDataLengthTests, ValidLength) {
  Http2GoAwayFields goaway;
  Randomize(&goaway, RandomPtr());
  std::string opaque_data = Random().RandString(length_);
  Http2FrameBuilder fb;
  fb.Append(goaway);
  fb.Append(opaque_data);
  Http2FrameHeader header(fb.size(), Http2FrameType::GOAWAY, RandFlags(),
                          RandStreamId());
  set_frame_header(header);
  FrameParts expected(header, opaque_data);
  expected.SetOptGoaway(goaway);
  ASSERT_TRUE(DecodePayloadAndValidateSeveralWays(fb.buffer(), expected));
}

}  // namespace
}  // namespace test
}  // namespace http2
