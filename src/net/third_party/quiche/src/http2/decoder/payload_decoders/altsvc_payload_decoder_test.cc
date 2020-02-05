// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/altsvc_payload_decoder.h"

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

// Provides friend access to an instance of the payload decoder, and also
// provides info to aid in testing.
class AltSvcPayloadDecoderPeer {
 public:
  static constexpr Http2FrameType FrameType() { return Http2FrameType::ALTSVC; }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() { return 0; }
};

namespace {

struct Listener : public FramePartsCollector {
  void OnAltSvcStart(const Http2FrameHeader& header,
                     size_t origin_length,
                     size_t value_length) override {
    HTTP2_VLOG(1) << "OnAltSvcStart header: " << header
                  << "; origin_length=" << origin_length
                  << "; value_length=" << value_length;
    StartFrame(header)->OnAltSvcStart(header, origin_length, value_length);
  }

  void OnAltSvcOriginData(const char* data, size_t len) override {
    HTTP2_VLOG(1) << "OnAltSvcOriginData: len=" << len;
    CurrentFrame()->OnAltSvcOriginData(data, len);
  }

  void OnAltSvcValueData(const char* data, size_t len) override {
    HTTP2_VLOG(1) << "OnAltSvcValueData: len=" << len;
    CurrentFrame()->OnAltSvcValueData(data, len);
  }

  void OnAltSvcEnd() override {
    HTTP2_VLOG(1) << "OnAltSvcEnd";
    EndFrame()->OnAltSvcEnd();
  }

  void OnFrameSizeError(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
    FrameError(header)->OnFrameSizeError(header);
  }
};

class AltSvcPayloadDecoderTest
    : public AbstractPayloadDecoderTest<AltSvcPayloadDecoder,
                                        AltSvcPayloadDecoderPeer,
                                        Listener> {};

// Confirm we get an error if the payload is not long enough to hold
// Http2AltSvcFields and the indicated length of origin.
TEST_F(AltSvcPayloadDecoderTest, Truncated) {
  Http2FrameBuilder fb;
  fb.Append(Http2AltSvcFields{0xffff});  // The longest possible origin length.
  fb.Append("Too little origin!");
  EXPECT_TRUE(
      VerifyDetectsFrameSizeError(0, fb.buffer(), /*approve_size*/ nullptr));
}

class AltSvcPayloadLengthTests : public AltSvcPayloadDecoderTest,
                                 public ::testing::WithParamInterface<
                                     ::testing::tuple<uint16_t, uint32_t>> {
 protected:
  AltSvcPayloadLengthTests()
      : origin_length_(::testing::get<0>(GetParam())),
        value_length_(::testing::get<1>(GetParam())) {
    HTTP2_VLOG(1) << "################  origin_length_=" << origin_length_
                  << "   value_length_=" << value_length_
                  << "  ################";
  }

  const uint16_t origin_length_;
  const uint32_t value_length_;
};

INSTANTIATE_TEST_SUITE_P(VariousOriginAndValueLengths,
                         AltSvcPayloadLengthTests,
                         ::testing::Combine(::testing::Values(0, 1, 3, 65535),
                                            ::testing::Values(0, 1, 3, 65537)));

TEST_P(AltSvcPayloadLengthTests, ValidOriginAndValueLength) {
  std::string origin = Random().RandString(origin_length_);
  std::string value = Random().RandString(value_length_);
  Http2FrameBuilder fb;
  fb.Append(Http2AltSvcFields{origin_length_});
  fb.Append(origin);
  fb.Append(value);
  Http2FrameHeader header(fb.size(), Http2FrameType::ALTSVC, RandFlags(),
                          RandStreamId());
  set_frame_header(header);
  FrameParts expected(header);
  expected.SetAltSvcExpected(origin, value);
  ASSERT_TRUE(DecodePayloadAndValidateSeveralWays(fb.buffer(), expected));
}

}  // namespace
}  // namespace test
}  // namespace http2
