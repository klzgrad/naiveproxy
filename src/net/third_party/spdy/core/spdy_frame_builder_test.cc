// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/spdy/core/spdy_frame_builder.h"

#include "net/third_party/spdy/core/array_output_buffer.h"
#include "net/third_party/spdy/core/spdy_framer.h"
#include "net/third_party/spdy/core/spdy_protocol.h"
#include "testing/platform_test.h"

namespace spdy {

namespace {

const int64_t kSize = 64 * 1024;
char output_buffer[kSize] = "";

}  // namespace

// Verifies that SpdyFrameBuilder::GetWritableBuffer() can be used to build a
// SpdySerializedFrame.
TEST(SpdyFrameBuilderTest, GetWritableBuffer) {
  const size_t kBuilderSize = 10;
  SpdyFrameBuilder builder(kBuilderSize);
  char* writable_buffer = builder.GetWritableBuffer(kBuilderSize);
  memset(writable_buffer, ~1, kBuilderSize);
  EXPECT_TRUE(builder.Seek(kBuilderSize));
  SpdySerializedFrame frame(builder.take());
  char expected[kBuilderSize];
  memset(expected, ~1, kBuilderSize);
  EXPECT_EQ(SpdyStringPiece(expected, kBuilderSize),
            SpdyStringPiece(frame.data(), kBuilderSize));
}

// Verifies that SpdyFrameBuilder::GetWritableBuffer() can be used to build a
// SpdySerializedFrame to the output buffer.
TEST(SpdyFrameBuilderTest, GetWritableOutput) {
  ArrayOutputBuffer output(output_buffer, kSize);
  const size_t kBuilderSize = 10;
  SpdyFrameBuilder builder(kBuilderSize, &output);
  size_t actual_size = 0;
  char* writable_buffer = builder.GetWritableOutput(kBuilderSize, &actual_size);
  memset(writable_buffer, ~1, kBuilderSize);
  EXPECT_TRUE(builder.Seek(kBuilderSize));
  SpdySerializedFrame frame(output.Begin(), kBuilderSize, false);
  char expected[kBuilderSize];
  memset(expected, ~1, kBuilderSize);
  EXPECT_EQ(SpdyStringPiece(expected, kBuilderSize),
            SpdyStringPiece(frame.data(), kBuilderSize));
}

// Verifies the case that the buffer's capacity is too small.
TEST(SpdyFrameBuilderTest, GetWritableOutputNegative) {
  size_t small_cap = 1;
  ArrayOutputBuffer output(output_buffer, small_cap);
  const size_t kBuilderSize = 10;
  SpdyFrameBuilder builder(kBuilderSize, &output);
  size_t actual_size = 0;
  char* writable_buffer = builder.GetWritableOutput(kBuilderSize, &actual_size);
  builder.GetWritableOutput(kBuilderSize, &actual_size);
  EXPECT_EQ(0u, actual_size);
  EXPECT_EQ(nullptr, writable_buffer);
}

}  // namespace spdy
