// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/window_update_payload_decoder.h"

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

class WindowUpdatePayloadDecoderPeer {
 public:
  static constexpr Http2FrameType FrameType() {
    return Http2FrameType::WINDOW_UPDATE;
  }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() { return 0; }
};

namespace {

struct Listener : public FramePartsCollector {
  void OnWindowUpdate(const Http2FrameHeader& header,
                      uint32_t window_size_increment) override {
    HTTP2_VLOG(1) << "OnWindowUpdate: " << header
                  << "; window_size_increment=" << window_size_increment;
    EXPECT_EQ(Http2FrameType::WINDOW_UPDATE, header.type);
    StartAndEndFrame(header)->OnWindowUpdate(header, window_size_increment);
  }

  void OnFrameSizeError(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
    FrameError(header)->OnFrameSizeError(header);
  }
};

class WindowUpdatePayloadDecoderTest
    : public AbstractPayloadDecoderTest<WindowUpdatePayloadDecoder,
                                        WindowUpdatePayloadDecoderPeer,
                                        Listener> {
 protected:
  Http2WindowUpdateFields RandWindowUpdateFields() {
    Http2WindowUpdateFields fields;
    test::Randomize(&fields, RandomPtr());
    HTTP2_VLOG(3) << "RandWindowUpdateFields: " << fields;
    return fields;
  }
};

// Confirm we get an error if the payload is not the correct size to hold
// exactly one Http2WindowUpdateFields.
TEST_F(WindowUpdatePayloadDecoderTest, WrongSize) {
  auto approve_size = [](size_t size) {
    return size != Http2WindowUpdateFields::EncodedSize();
  };
  Http2FrameBuilder fb;
  fb.Append(RandWindowUpdateFields());
  fb.Append(RandWindowUpdateFields());
  fb.Append(RandWindowUpdateFields());
  EXPECT_TRUE(VerifyDetectsFrameSizeError(0, fb.buffer(), approve_size));
}

TEST_F(WindowUpdatePayloadDecoderTest, VariousPayloads) {
  for (int n = 0; n < 100; ++n) {
    uint32_t stream_id = n == 0 ? 0 : RandStreamId();
    Http2WindowUpdateFields fields = RandWindowUpdateFields();
    Http2FrameBuilder fb;
    fb.Append(fields);
    Http2FrameHeader header(fb.size(), Http2FrameType::WINDOW_UPDATE,
                            RandFlags(), stream_id);
    set_frame_header(header);
    FrameParts expected(header);
    expected.SetOptWindowUpdateIncrement(fields.window_size_increment);
    EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(fb.buffer(), expected));
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
