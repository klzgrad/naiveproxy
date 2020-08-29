// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_deframer_visitor.h"

#include <stdlib.h>

#include <algorithm>
#include <limits>

#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_constants.h"
#include "net/third_party/quiche/src/spdy/core/mock_spdy_framer_visitor.h"
#include "net/third_party/quiche/src/spdy/core/spdy_frame_builder.h"
#include "net/third_party/quiche/src/spdy/core/spdy_frame_reader.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol_test_utils.h"
#include "net/third_party/quiche/src/spdy/core/spdy_test_utils.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"

namespace spdy {
namespace test {
namespace {

class SpdyDeframerVisitorTest : public QuicheTest {
 protected:
  SpdyDeframerVisitorTest() : encoder_(SpdyFramer::ENABLE_COMPRESSION) {
    decoder_.set_process_single_input_frame(true);
    auto collector =
        std::make_unique<DeframerCallbackCollector>(&collected_frames_);
    auto log_and_collect =
        SpdyDeframerVisitorInterface::LogBeforeVisiting(std::move(collector));
    deframer_ = SpdyTestDeframer::CreateConverter(std::move(log_and_collect));
    decoder_.set_visitor(deframer_.get());
  }

  bool DeframeInput(const char* input, size_t size) {
    size_t input_remaining = size;
    while (input_remaining > 0 &&
           decoder_.spdy_framer_error() ==
               http2::Http2DecoderAdapter::SPDY_NO_ERROR) {
      // To make the tests more interesting, we feed random (and small) chunks
      // into the framer.  This simulates getting strange-sized reads from
      // the socket.
      const size_t kMaxReadSize = 32;
      size_t bytes_read =
          (random_.Uniform(std::min(input_remaining, kMaxReadSize))) + 1;
      size_t bytes_processed = decoder_.ProcessInput(input, bytes_read);
      input_remaining -= bytes_processed;
      input += bytes_processed;
      if (decoder_.state() ==
          http2::Http2DecoderAdapter::SPDY_READY_FOR_FRAME) {
        deframer_->AtFrameEnd();
      }
    }
    return (input_remaining == 0 &&
            decoder_.spdy_framer_error() ==
                http2::Http2DecoderAdapter::SPDY_NO_ERROR);
  }

  SpdyFramer encoder_;
  http2::Http2DecoderAdapter decoder_;
  std::vector<CollectedFrame> collected_frames_;
  std::unique_ptr<SpdyTestDeframer> deframer_;

 private:
  http2::test::Http2Random random_;
};

TEST_F(SpdyDeframerVisitorTest, DataFrame) {
  const char kFrameData[] = {
      0x00, 0x00, 0x0d,        // Length = 13.
      0x00,                    // DATA
      0x08,                    // PADDED
      0x00, 0x00, 0x00, 0x01,  // Stream 1
      0x07,                    // Pad length field.
      'h',  'e',  'l',  'l',   // Data
      'o',                     // More Data
      0x00, 0x00, 0x00, 0x00,  // Padding
      0x00, 0x00, 0x00         // More Padding
  };

  EXPECT_TRUE(DeframeInput(kFrameData, sizeof kFrameData));
  ASSERT_EQ(1u, collected_frames_.size());
  const CollectedFrame& cf0 = collected_frames_[0];
  ASSERT_NE(cf0.frame_ir, nullptr);

  SpdyDataIR expected_ir(/* stream_id = */ 1, "hello");
  expected_ir.set_padding_len(8);
  EXPECT_TRUE(cf0.VerifyHasFrame(expected_ir));
}

TEST_F(SpdyDeframerVisitorTest, HeaderFrameWithContinuation) {
  const char kFrameData[] = {
      0x00, 0x00, 0x05,        // Payload Length: 5
      0x01,                    // Type: HEADERS
      0x09,                    // Flags: PADDED | END_STREAM
      0x00, 0x00, 0x00, 0x01,  // Stream: 1
      0x04,                    // Padding Length: 4
      0x00, 0x00, 0x00, 0x00,  // Padding
      /* Second Frame */
      0x00, 0x00, 0x12,        // Payload Length: 18
      0x09,                    // Type: CONTINUATION
      0x04,                    // Flags: END_HEADERS
      0x00, 0x00, 0x00, 0x01,  // Stream: 1
      0x00,                    // Unindexed, literal name & value
      0x03, 0x62, 0x61, 0x72,  // Name len and name (3, "bar")
      0x03, 0x66, 0x6f, 0x6f,  // Value len and value (3, "foo")
      0x00,                    // Unindexed, literal name & value
      0x03, 0x66, 0x6f, 0x6f,  // Name len and name (3, "foo")
      0x03, 0x62, 0x61, 0x72,  // Value len and value (3, "bar")
  };

  EXPECT_TRUE(DeframeInput(kFrameData, sizeof kFrameData));
  ASSERT_EQ(1u, collected_frames_.size());
  const CollectedFrame& cf0 = collected_frames_[0];

  StringPairVector headers;
  headers.push_back({"bar", "foo"});
  headers.push_back({"foo", "bar"});

  EXPECT_TRUE(cf0.VerifyHasHeaders(headers));

  SpdyHeadersIR expected_ir(/* stream_id = */ 1);
  // Yet again SpdyFramerVisitorInterface is lossy: it doesn't call OnPadding
  // for HEADERS, just for DATA. Sigh.
  //    expected_ir.set_padding_len(5);
  expected_ir.set_fin(true);
  for (const auto& nv : headers) {
    expected_ir.SetHeader(nv.first, nv.second);
  }

  EXPECT_TRUE(cf0.VerifyHasFrame(expected_ir));

  // Confirm that mismatches are also detected.
  headers.push_back({"baz", "bing"});
  EXPECT_FALSE(cf0.VerifyHasHeaders(headers));
  EXPECT_TRUE(cf0.VerifyHasFrame(expected_ir));

  headers.pop_back();
  EXPECT_TRUE(cf0.VerifyHasHeaders(headers));
  EXPECT_TRUE(cf0.VerifyHasFrame(expected_ir));

  expected_ir.SetHeader("baz", "bing");
  EXPECT_FALSE(cf0.VerifyHasFrame(expected_ir));
  EXPECT_TRUE(cf0.VerifyHasHeaders(headers));
}

TEST_F(SpdyDeframerVisitorTest, PriorityFrame) {
  const char kFrameData[] = {
      0x00,   0x00, 0x05,        // Length: 5
      0x02,                      //   Type: PRIORITY
      0x00,                      //  Flags: none
      0x00,   0x00, 0x00, 0x65,  // Stream: 101
      '\x80', 0x00, 0x00, 0x01,  // Parent: 1 (Exclusive)
      0x10,                      // Weight: 17
  };

  EXPECT_TRUE(DeframeInput(kFrameData, sizeof kFrameData));
  ASSERT_EQ(1u, collected_frames_.size());
  const CollectedFrame& cf0 = collected_frames_[0];

  SpdyPriorityIR expected_ir(/* stream_id = */ 101,
                             /* parent_stream_id = */ 1, /* weight = */ 17,
                             /* exclusive = */ true);
  EXPECT_TRUE(cf0.VerifyHasFrame(expected_ir));

  // Confirm that mismatches are also detected.
  EXPECT_FALSE(cf0.VerifyHasFrame(SpdyPriorityIR(101, 1, 16, true)));
  EXPECT_FALSE(cf0.VerifyHasFrame(SpdyPriorityIR(101, 50, 17, true)));
  EXPECT_FALSE(cf0.VerifyHasFrame(SpdyPriorityIR(201, 1, 17, true)));
  EXPECT_FALSE(cf0.VerifyHasFrame(SpdyPriorityIR(101, 1, 17, false)));
}

TEST_F(SpdyDeframerVisitorTest, DISABLED_RstStreamFrame) {
  // TODO(jamessynge): Please implement.
}

TEST_F(SpdyDeframerVisitorTest, SettingsFrame) {
  // Settings frame with two entries for the same parameter but with different
  // values. The last one will be in the decoded SpdySettingsIR, but the vector
  // of settings will have both, in the same order.
  const char kFrameData[] = {
      0x00, 0x00, 0x0c,          // Length
      0x04,                      // Type (SETTINGS)
      0x00,                      // Flags
      0x00, 0x00, 0x00, 0x00,    // Stream id (must be zero)
      0x00, 0x04,                // Setting id (SETTINGS_INITIAL_WINDOW_SIZE)
      0x0a, 0x0b, 0x0c, 0x0d,    // Setting value
      0x00, 0x04,                // Setting id (SETTINGS_INITIAL_WINDOW_SIZE)
      0x00, 0x00, 0x00, '\xff',  // Setting value
  };

  EXPECT_TRUE(DeframeInput(kFrameData, sizeof kFrameData));
  ASSERT_EQ(1u, collected_frames_.size());
  const CollectedFrame& cf0 = collected_frames_[0];
  ASSERT_NE(cf0.frame_ir, nullptr);

  SpdySettingsIR expected_ir;
  expected_ir.AddSetting(SETTINGS_INITIAL_WINDOW_SIZE, 255);
  EXPECT_TRUE(cf0.VerifyHasFrame(expected_ir));

  SettingVector expected_settings;
  expected_settings.push_back({SETTINGS_INITIAL_WINDOW_SIZE, 0x0a0b0c0d});
  expected_settings.push_back({SETTINGS_INITIAL_WINDOW_SIZE, 255});

  EXPECT_TRUE(cf0.VerifyHasSettings(expected_settings));

  // Confirm that mismatches are also detected.
  expected_settings.push_back({SETTINGS_INITIAL_WINDOW_SIZE, 65536});
  EXPECT_FALSE(cf0.VerifyHasSettings(expected_settings));

  expected_ir.AddSetting(SETTINGS_INITIAL_WINDOW_SIZE, 65536);
  EXPECT_FALSE(cf0.VerifyHasFrame(expected_ir));

  SpdySettingsIR unexpected_ir;
  unexpected_ir.set_is_ack(true);
  EXPECT_FALSE(cf0.VerifyHasFrame(unexpected_ir));
}

TEST_F(SpdyDeframerVisitorTest, DISABLED_PushPromiseFrame) {
  // TODO(jamessynge): Please implement.
}

TEST_F(SpdyDeframerVisitorTest, DISABLED_PingFrame) {
  // TODO(jamessynge): Please implement.
}

TEST_F(SpdyDeframerVisitorTest, DISABLED_GoAwayFrame) {
  // TODO(jamessynge): Please implement.
}

TEST_F(SpdyDeframerVisitorTest, DISABLED_WindowUpdateFrame) {
  // TODO(jamessynge): Please implement.
}

TEST_F(SpdyDeframerVisitorTest, DISABLED_AltSvcFrame) {
  // TODO(jamessynge): Please implement.
}

}  // namespace
}  // namespace test
}  // namespace spdy
