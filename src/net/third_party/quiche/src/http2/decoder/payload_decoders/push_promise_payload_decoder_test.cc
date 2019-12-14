// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/push_promise_payload_decoder.h"

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
class PushPromisePayloadDecoderPeer {
 public:
  static constexpr Http2FrameType FrameType() {
    return Http2FrameType::PUSH_PROMISE;
  }

  // Returns the mask of flags that affect the decoding of the payload (i.e.
  // flags that that indicate the presence of certain fields or padding).
  static constexpr uint8_t FlagsAffectingPayloadDecoding() {
    return Http2FrameFlag::PADDED;
  }
};

namespace {

// Listener listens for only those methods expected by the payload decoder
// under test, and forwards them onto the FrameParts instance for the current
// frame.
struct Listener : public FramePartsCollector {
  void OnPushPromiseStart(const Http2FrameHeader& header,
                          const Http2PushPromiseFields& promise,
                          size_t total_padding_length) override {
    HTTP2_VLOG(1) << "OnPushPromiseStart header: " << header
                  << "  promise: " << promise
                  << "  total_padding_length: " << total_padding_length;
    EXPECT_EQ(Http2FrameType::PUSH_PROMISE, header.type);
    StartFrame(header)->OnPushPromiseStart(header, promise,
                                           total_padding_length);
  }

  void OnHpackFragment(const char* data, size_t len) override {
    HTTP2_VLOG(1) << "OnHpackFragment: len=" << len;
    CurrentFrame()->OnHpackFragment(data, len);
  }

  void OnPushPromiseEnd() override {
    HTTP2_VLOG(1) << "OnPushPromiseEnd";
    EndFrame()->OnPushPromiseEnd();
  }

  void OnPadding(const char* padding, size_t skipped_length) override {
    HTTP2_VLOG(1) << "OnPadding: " << skipped_length;
    CurrentFrame()->OnPadding(padding, skipped_length);
  }

  void OnPaddingTooLong(const Http2FrameHeader& header,
                        size_t missing_length) override {
    HTTP2_VLOG(1) << "OnPaddingTooLong: " << header
                  << "; missing_length: " << missing_length;
    FrameError(header)->OnPaddingTooLong(header, missing_length);
  }

  void OnFrameSizeError(const Http2FrameHeader& header) override {
    HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
    FrameError(header)->OnFrameSizeError(header);
  }
};

class PushPromisePayloadDecoderTest
    : public AbstractPaddablePayloadDecoderTest<PushPromisePayloadDecoder,
                                                PushPromisePayloadDecoderPeer,
                                                Listener> {};

INSTANTIATE_TEST_SUITE_P(VariousPadLengths,
                         PushPromisePayloadDecoderTest,
                         ::testing::Values(0, 1, 2, 3, 4, 254, 255, 256));

// Payload contains the required Http2PushPromiseFields, followed by some
// (fake) HPACK payload.
TEST_P(PushPromisePayloadDecoderTest, VariousHpackPayloadSizes) {
  for (size_t hpack_size : {0, 1, 2, 3, 255, 256, 1024}) {
    HTTP2_LOG(INFO) << "###########   hpack_size = " << hpack_size
                    << "  ###########";
    Reset();
    std::string hpack_payload = Random().RandString(hpack_size);
    Http2PushPromiseFields push_promise{RandStreamId()};
    frame_builder_.Append(push_promise);
    frame_builder_.Append(hpack_payload);
    MaybeAppendTrailingPadding();
    Http2FrameHeader frame_header(frame_builder_.size(),
                                  Http2FrameType::PUSH_PROMISE, RandFlags(),
                                  RandStreamId());
    set_frame_header(frame_header);
    FrameParts expected(frame_header, hpack_payload, total_pad_length_);
    expected.SetOptPushPromise(push_promise);
    EXPECT_TRUE(
        DecodePayloadAndValidateSeveralWays(frame_builder_.buffer(), expected));
  }
}

// Confirm we get an error if the payload is not long enough for the required
// portion of the payload, regardless of the amount of (valid) padding.
TEST_P(PushPromisePayloadDecoderTest, Truncated) {
  auto approve_size = [](size_t size) {
    return size != Http2PushPromiseFields::EncodedSize();
  };
  Http2PushPromiseFields push_promise{RandStreamId()};
  Http2FrameBuilder fb;
  fb.Append(push_promise);
  EXPECT_TRUE(VerifyDetectsMultipleFrameSizeErrors(0, fb.buffer(), approve_size,
                                                   total_pad_length_));
}

// Confirm we get an error if the PADDED flag is set but the payload is not
// long enough to hold even the Pad Length amount of padding.
TEST_P(PushPromisePayloadDecoderTest, PaddingTooLong) {
  EXPECT_TRUE(VerifyDetectsPaddingTooLong());
}

}  // namespace
}  // namespace test
}  // namespace http2
