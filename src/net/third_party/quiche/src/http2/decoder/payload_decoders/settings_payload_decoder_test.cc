// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/settings_payload_decoder.h"

#include <stddef.h>

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/payload_decoder_base_test_util.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_constants_test_util.h"
#include "net/third_party/quiche/src/http2/http2_structures_test_util.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts_collector.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/http2_frame_builder.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"

namespace http2 {
namespace test {

class SettingsPayloadDecoderPeer {
 public:
  static constexpr Http2FrameType FrameType() {
    return Http2FrameType::SETTINGS;
  }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() {
    return Http2FrameFlag::ACK;
  }
};

namespace {

struct Listener : public FramePartsCollector {
  void OnSettingsStart(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnSettingsStart: " << header;
    EXPECT_EQ(Http2FrameType::SETTINGS, header.type) << header;
    EXPECT_EQ(Http2FrameFlag(), header.flags) << header;
    StartFrame(header)->OnSettingsStart(header);
  }

  void OnSetting(const Http2SettingFields& setting_fields) override {
    HTTP2_VLOG(1) << "Http2SettingFields: setting_fields=" << setting_fields;
    CurrentFrame()->OnSetting(setting_fields);
  }

  void OnSettingsEnd() override {
    HTTP2_VLOG(1) << "OnSettingsEnd";
    EndFrame()->OnSettingsEnd();
  }

  void OnSettingsAck(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnSettingsAck: " << header;
    StartAndEndFrame(header)->OnSettingsAck(header);
  }

  void OnFrameSizeError(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
    FrameError(header)->OnFrameSizeError(header);
  }
};

class SettingsPayloadDecoderTest
    : public AbstractPayloadDecoderTest<SettingsPayloadDecoder,
                                        SettingsPayloadDecoderPeer,
                                        Listener> {
 protected:
  Http2SettingFields RandSettingsFields() {
    Http2SettingFields fields;
    test::Randomize(&fields, RandomPtr());
    return fields;
  }
};

// Confirm we get an error if the SETTINGS payload is not the correct size
// to hold exactly zero or more whole Http2SettingFields.
TEST_F(SettingsPayloadDecoderTest, SettingsWrongSize) {
  auto approve_size = [](size_t size) {
    // Should get an error if size is not an integral multiple of the size
    // of one setting.
    return 0 != (size % Http2SettingFields::EncodedSize());
  };
  Http2FrameBuilder fb;
  fb.Append(RandSettingsFields());
  fb.Append(RandSettingsFields());
  fb.Append(RandSettingsFields());
  EXPECT_TRUE(VerifyDetectsFrameSizeError(0, fb.buffer(), approve_size));
}

// Confirm we get an error if the SETTINGS ACK payload is not empty.
TEST_F(SettingsPayloadDecoderTest, SettingsAkcWrongSize) {
  auto approve_size = [](size_t size) { return size != 0; };
  Http2FrameBuilder fb;
  fb.Append(RandSettingsFields());
  fb.Append(RandSettingsFields());
  fb.Append(RandSettingsFields());
  EXPECT_TRUE(VerifyDetectsFrameSizeError(Http2FrameFlag::ACK, fb.buffer(),
                                          approve_size));
}

// SETTINGS must have stream_id==0, but the payload decoder doesn't check that.
TEST_F(SettingsPayloadDecoderTest, SettingsAck) {
  for (int stream_id = 0; stream_id < 3; ++stream_id) {
    Http2FrameHeader header(0, Http2FrameType::SETTINGS,
                            RandFlags() | Http2FrameFlag::ACK, stream_id);
    set_frame_header(header);
    FrameParts expected(header);
    EXPECT_TRUE(DecodePayloadAndValidateSeveralWays("", expected));
  }
}

// Try several values of each known SETTINGS parameter.
TEST_F(SettingsPayloadDecoderTest, OneRealSetting) {
  std::vector<uint32_t> values = {0, 1, 0xffffffff, Random().Rand32()};
  for (auto param : AllHttp2SettingsParameters()) {
    for (uint32_t value : values) {
      Http2SettingFields fields(param, value);
      Http2FrameBuilder fb;
      fb.Append(fields);
      Http2FrameHeader header(fb.size(), Http2FrameType::SETTINGS, RandFlags(),
                              RandStreamId());
      set_frame_header(header);
      FrameParts expected(header);
      expected.AppendSetting(fields);
      EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(fb.buffer(), expected));
    }
  }
}

// Decode a SETTINGS frame with lots of fields.
TEST_F(SettingsPayloadDecoderTest, ManySettings) {
  const size_t num_settings = 100;
  const size_t size = Http2SettingFields::EncodedSize() * num_settings;
  Http2FrameHeader header(size, Http2FrameType::SETTINGS,
                          RandFlags(),  // & ~Http2FrameFlag::ACK,
                          RandStreamId());
  set_frame_header(header);
  FrameParts expected(header);
  Http2FrameBuilder fb;
  for (size_t n = 0; n < num_settings; ++n) {
    Http2SettingFields fields(static_cast<Http2SettingsParameter>(n),
                              Random().Rand32());
    fb.Append(fields);
    expected.AppendSetting(fields);
  }
  ASSERT_EQ(size, fb.size());
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(fb.buffer(), expected));
}

}  // namespace
}  // namespace test
}  // namespace http2
