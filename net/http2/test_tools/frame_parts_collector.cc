// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/test_tools/frame_parts_collector.h"

#include <utility>

#include "base/logging.h"
#include "net/http2/http2_structures_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

FramePartsCollector::FramePartsCollector() {}
FramePartsCollector::~FramePartsCollector() {}

void FramePartsCollector::Reset() {
  current_frame_.reset();
  collected_frames_.clear();
  expected_header_set_ = false;
}

const FrameParts* FramePartsCollector::frame(size_t n) const {
  if (n < size()) {
    return collected_frames_.at(n).get();
  }
  CHECK(n == size());
  return current_frame();
}

void FramePartsCollector::ExpectFrameHeader(const Http2FrameHeader& header) {
  EXPECT_FALSE(IsInProgress());
  EXPECT_FALSE(expected_header_set_) << "expected_header_: "
                                     << expected_header_;
  expected_header_ = header;
  expected_header_set_ = true;
  // OnFrameHeader is called before the flags are scrubbed, but the other
  // methods are called after, so scrub the invalid flags from expected_header_.
  ScrubFlagsOfHeader(&expected_header_);
}

void FramePartsCollector::TestExpectedHeader(const Http2FrameHeader& header) {
  if (expected_header_set_) {
    EXPECT_EQ(header, expected_header_);
    expected_header_set_ = false;
  }
}

Http2FrameDecoderListener* FramePartsCollector::StartFrame(
    const Http2FrameHeader& header) {
  TestExpectedHeader(header);
  EXPECT_FALSE(IsInProgress());
  if (current_frame_ == nullptr) {
    current_frame_.reset(new FrameParts(header));
  }
  return current_frame();
}

Http2FrameDecoderListener* FramePartsCollector::StartAndEndFrame(
    const Http2FrameHeader& header) {
  TestExpectedHeader(header);
  EXPECT_FALSE(IsInProgress());
  if (current_frame_ == nullptr) {
    current_frame_.reset(new FrameParts(header));
  }
  Http2FrameDecoderListener* result = current_frame();
  collected_frames_.push_back(std::move(current_frame_));
  return result;
}

Http2FrameDecoderListener* FramePartsCollector::CurrentFrame() {
  EXPECT_TRUE(IsInProgress());
  if (current_frame_ == nullptr) {
    return &failing_listener_;
  }
  return current_frame();
}

Http2FrameDecoderListener* FramePartsCollector::EndFrame() {
  EXPECT_TRUE(IsInProgress());
  if (current_frame_ == nullptr) {
    return &failing_listener_;
  }
  Http2FrameDecoderListener* result = current_frame();
  collected_frames_.push_back(std::move(current_frame_));
  return result;
}

Http2FrameDecoderListener* FramePartsCollector::FrameError(
    const Http2FrameHeader& header) {
  TestExpectedHeader(header);
  if (current_frame_ == nullptr) {
    // The decoder may detect an error before making any calls to the listener
    // regarding the frame, in which case current_frame_==nullptr and we need
    // to create a FrameParts instance.
    current_frame_.reset(new FrameParts(header));
  } else {
    // Similarly, the decoder may have made calls to the listener regarding the
    // frame before detecting the error; for example, the DATA payload decoder
    // calls OnDataStart before it can detect padding errors, hence before it
    // can call OnPaddingTooLong.
    EXPECT_EQ(header, current_frame_->frame_header);
  }
  Http2FrameDecoderListener* result = current_frame();
  collected_frames_.push_back(std::move(current_frame_));
  return result;
}

}  // namespace test
}  // namespace net
