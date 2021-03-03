// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "http2/decoder/payload_decoders/priority_update_payload_decoder.h"

#include <stddef.h>

#include <string>

#include "http2/decoder/http2_frame_decoder_listener.h"
#include "http2/decoder/payload_decoders/payload_decoder_base_test_util.h"
#include "http2/http2_constants.h"
#include "http2/http2_structures_test_util.h"
#include "http2/platform/api/http2_logging.h"
#include "http2/test_tools/frame_parts.h"
#include "http2/test_tools/frame_parts_collector.h"
#include "http2/test_tools/http2_random.h"
#include "http2/tools/http2_frame_builder.h"
#include "http2/tools/random_decoder_test.h"
#include "common/platform/api/quiche_test.h"

namespace http2 {
namespace test {

class PriorityUpdatePayloadDecoderPeer {
 public:
  static constexpr Http2FrameType FrameType() {
    return Http2FrameType::PRIORITY_UPDATE;
  }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() { return 0; }
};

namespace {

struct Listener : public FramePartsCollector {
  void OnPriorityUpdateStart(
      const Http2FrameHeader& header,
      const Http2PriorityUpdateFields& priority_update) override {
    HTTP2_VLOG(1) << "OnPriorityUpdateStart header: " << header
                  << "; priority_update: " << priority_update;
    StartFrame(header)->OnPriorityUpdateStart(header, priority_update);
  }

  void OnPriorityUpdatePayload(const char* data, size_t len) override {
    HTTP2_VLOG(1) << "OnPriorityUpdatePayload: len=" << len;
    CurrentFrame()->OnPriorityUpdatePayload(data, len);
  }

  void OnPriorityUpdateEnd() override {
    HTTP2_VLOG(1) << "OnPriorityUpdateEnd";
    EndFrame()->OnPriorityUpdateEnd();
  }

  void OnFrameSizeError(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
    FrameError(header)->OnFrameSizeError(header);
  }
};

// Avoid initialization of test class when flag is false, because base class
// method AbstractPayloadDecoderTest::SetUp() crashes if
// IsSupportedHttp2FrameType(PRIORITY_UPDATE) returns false.
std::vector<bool> GetTestParams() {
  if (GetHttp2RestartFlag(http2_parse_priority_update_frame)) {
    return {true};  // Actual Boolean value is ignored.
  } else {
    return {};
  }
}

class PriorityUpdatePayloadDecoderTest
    : public AbstractPayloadDecoderTest<PriorityUpdatePayloadDecoder,
                                        PriorityUpdatePayloadDecoderPeer,
                                        Listener>,
      public ::testing::WithParamInterface<bool> {};

INSTANTIATE_TEST_SUITE_P(MaybeRunTest,
                         PriorityUpdatePayloadDecoderTest,
                         ::testing::ValuesIn(GetTestParams()));
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(PriorityUpdatePayloadDecoderTest);

// Confirm we get an error if the payload is not long enough to hold
// Http2PriorityUpdateFields.
TEST_P(PriorityUpdatePayloadDecoderTest, Truncated) {
  auto approve_size = [](size_t size) {
    return size != Http2PriorityUpdateFields::EncodedSize();
  };
  Http2FrameBuilder fb;
  fb.Append(Http2PriorityUpdateFields(123));
  EXPECT_TRUE(VerifyDetectsFrameSizeError(0, fb.buffer(), approve_size));
}

class PriorityUpdatePayloadLengthTests
    : public AbstractPayloadDecoderTest<PriorityUpdatePayloadDecoder,
                                        PriorityUpdatePayloadDecoderPeer,
                                        Listener>,
      public ::testing::WithParamInterface<std::tuple<uint32_t, bool>> {
 protected:
  PriorityUpdatePayloadLengthTests() : length_(std::get<0>(GetParam())) {
    HTTP2_VLOG(1) << "################  length_=" << length_
                  << "  ################";
  }

  const uint32_t length_;
};

INSTANTIATE_TEST_SUITE_P(
    VariousLengths,
    PriorityUpdatePayloadLengthTests,
    ::testing::Combine(::testing::Values(0, 1, 2, 3, 4, 5, 6),
                       ::testing::ValuesIn(GetTestParams())));
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(PriorityUpdatePayloadLengthTests);

TEST_P(PriorityUpdatePayloadLengthTests, ValidLength) {
  Http2PriorityUpdateFields priority_update;
  Randomize(&priority_update, RandomPtr());
  std::string priority_field_value = Random().RandString(length_);
  Http2FrameBuilder fb;
  fb.Append(priority_update);
  fb.Append(priority_field_value);
  Http2FrameHeader header(fb.size(), Http2FrameType::PRIORITY_UPDATE,
                          RandFlags(), RandStreamId());
  set_frame_header(header);
  FrameParts expected(header, priority_field_value);
  expected.SetOptPriorityUpdate(Http2PriorityUpdateFields{priority_update});
  ASSERT_TRUE(DecodePayloadAndValidateSeveralWays(fb.buffer(), expected));
}

}  // namespace
}  // namespace test
}  // namespace http2
