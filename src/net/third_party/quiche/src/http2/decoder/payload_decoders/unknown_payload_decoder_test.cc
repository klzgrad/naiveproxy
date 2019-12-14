// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/unknown_payload_decoder.h"

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
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"

namespace http2 {
namespace test {
namespace {
Http2FrameType g_unknown_frame_type;
}  // namespace

// Provides friend access to an instance of the payload decoder, and also
// provides info to aid in testing.
class UnknownPayloadDecoderPeer {
 public:
  static Http2FrameType FrameType() { return g_unknown_frame_type; }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() { return 0; }
};

namespace {

struct Listener : public FramePartsCollector {
  void OnUnknownStart(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnUnknownStart: " << header;
    StartFrame(header)->OnUnknownStart(header);
  }

  void OnUnknownPayload(const char* data, size_t len) override {
    HTTP2_VLOG(1) << "OnUnknownPayload: len=" << len;
    CurrentFrame()->OnUnknownPayload(data, len);
  }

  void OnUnknownEnd() override {
    HTTP2_VLOG(1) << "OnUnknownEnd";
    EndFrame()->OnUnknownEnd();
  }
};

constexpr bool SupportedFrameType = false;

class UnknownPayloadDecoderTest
    : public AbstractPayloadDecoderTest<UnknownPayloadDecoder,
                                        UnknownPayloadDecoderPeer,
                                        Listener,
                                        SupportedFrameType>,
      public ::testing::WithParamInterface<uint32_t> {
 protected:
  UnknownPayloadDecoderTest() : length_(GetParam()) {
    HTTP2_VLOG(1) << "################  length_=" << length_
                  << "  ################";

    // Each test case will choose a random frame type that isn't supported.
    do {
      g_unknown_frame_type = static_cast<Http2FrameType>(Random().Rand8());
    } while (IsSupportedHttp2FrameType(g_unknown_frame_type));
  }

  const uint32_t length_;
};

INSTANTIATE_TEST_SUITE_P(VariousLengths,
                         UnknownPayloadDecoderTest,
                         ::testing::Values(0, 1, 2, 3, 255, 256));

TEST_P(UnknownPayloadDecoderTest, ValidLength) {
  std::string unknown_payload = Random().RandString(length_);
  Http2FrameHeader frame_header(length_, g_unknown_frame_type, Random().Rand8(),
                                RandStreamId());
  set_frame_header(frame_header);
  FrameParts expected(frame_header, unknown_payload);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(unknown_payload, expected));
  // TODO(jamessynge): Check here (and in other such tests) that the fast
  // and slow decode counts are both non-zero. Perhaps also add some kind of
  // test for the listener having been called. That could simply be a test
  // that there is a single collected FrameParts instance, and that it matches
  // expected.
}

}  // namespace
}  // namespace test
}  // namespace http2
