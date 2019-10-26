// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/ping_payload_decoder.h"

#include <stddef.h>

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

class PingPayloadDecoderPeer {
 public:
  static constexpr Http2FrameType FrameType() { return Http2FrameType::PING; }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() { return 0; }
};

namespace {

struct Listener : public FramePartsCollector {
  void OnPing(const Http2FrameHeader& header,
              const Http2PingFields& ping) override {
    HTTP2_VLOG(1) << "OnPing: " << header << "; " << ping;
    StartAndEndFrame(header)->OnPing(header, ping);
  }

  void OnPingAck(const Http2FrameHeader& header,
                 const Http2PingFields& ping) override {
    HTTP2_VLOG(1) << "OnPingAck: " << header << "; " << ping;
    StartAndEndFrame(header)->OnPingAck(header, ping);
  }

  void OnFrameSizeError(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
    FrameError(header)->OnFrameSizeError(header);
  }
};

class PingPayloadDecoderTest
    : public AbstractPayloadDecoderTest<PingPayloadDecoder,
                                        PingPayloadDecoderPeer,
                                        Listener> {
 protected:
  Http2PingFields RandPingFields() {
    Http2PingFields fields;
    test::Randomize(&fields, RandomPtr());
    return fields;
  }
};

// Confirm we get an error if the payload is not the correct size to hold
// exactly one Http2PingFields.
TEST_F(PingPayloadDecoderTest, WrongSize) {
  auto approve_size = [](size_t size) {
    return size != Http2PingFields::EncodedSize();
  };
  Http2FrameBuilder fb;
  fb.Append(RandPingFields());
  fb.Append(RandPingFields());
  fb.Append(RandPingFields());
  EXPECT_TRUE(VerifyDetectsFrameSizeError(0, fb.buffer(), approve_size));
}

TEST_F(PingPayloadDecoderTest, Ping) {
  for (int n = 0; n < 100; ++n) {
    Http2PingFields fields = RandPingFields();
    Http2FrameBuilder fb;
    fb.Append(fields);
    Http2FrameHeader header(fb.size(), Http2FrameType::PING,
                            RandFlags() & ~Http2FrameFlag::ACK, RandStreamId());
    set_frame_header(header);
    FrameParts expected(header);
    expected.SetOptPing(fields);
    EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(fb.buffer(), expected));
  }
}

TEST_F(PingPayloadDecoderTest, PingAck) {
  for (int n = 0; n < 100; ++n) {
    Http2PingFields fields;
    Randomize(&fields, RandomPtr());
    Http2FrameBuilder fb;
    fb.Append(fields);
    Http2FrameHeader header(fb.size(), Http2FrameType::PING,
                            RandFlags() | Http2FrameFlag::ACK, RandStreamId());
    set_frame_header(header);
    FrameParts expected(header);
    expected.SetOptPing(fields);
    EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(fb.buffer(), expected));
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
