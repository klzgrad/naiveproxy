// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream_body_buffer.h"

#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {

namespace test {

namespace {

class QuicSpdyStreamBodyBufferTest : public QuicTest {
 protected:
  QuicSpdyStreamBodyBuffer body_buffer_;
};

TEST_F(QuicSpdyStreamBodyBufferTest, ReceiveBodies) {
  std::string body(1024, 'a');
  EXPECT_FALSE(body_buffer_.HasBytesToRead());
  body_buffer_.OnDataHeader(Http3FrameLengths(3, 1024));
  body_buffer_.OnDataPayload(body);
  EXPECT_EQ(1024u, body_buffer_.total_body_bytes_received());
  EXPECT_TRUE(body_buffer_.HasBytesToRead());
}

TEST_F(QuicSpdyStreamBodyBufferTest, PeekBody) {
  std::string body(1024, 'a');
  body_buffer_.OnDataHeader(Http3FrameLengths(3, 1024));
  body_buffer_.OnDataPayload(body);
  EXPECT_EQ(1024u, body_buffer_.total_body_bytes_received());
  iovec vec;
  EXPECT_EQ(1, body_buffer_.PeekBody(&vec, 1));
  EXPECT_EQ(1024u, vec.iov_len);
  EXPECT_EQ(body,
            QuicStringPiece(static_cast<const char*>(vec.iov_base), 1024));
}

// Buffer only receives 1 frame. Stream consumes less or equal than a frame.
TEST_F(QuicSpdyStreamBodyBufferTest, MarkConsumedPartialSingleFrame) {
  testing::InSequence seq;
  std::string body(1024, 'a');
  const QuicByteCount header_length = 3;
  Http3FrameLengths lengths(header_length, 1024);
  body_buffer_.OnDataHeader(lengths);
  body_buffer_.OnDataPayload(body);
  EXPECT_EQ(header_length + 1024, body_buffer_.OnBodyConsumed(1024));
}

// Buffer received 2 frames. Stream consumes multiple times.
TEST_F(QuicSpdyStreamBodyBufferTest, MarkConsumedMultipleFrames) {
  testing::InSequence seq;
  // 1st frame.
  std::string body1(1024, 'a');
  const QuicByteCount header_length1 = 2;
  Http3FrameLengths lengths1(header_length1, 1024);
  body_buffer_.OnDataHeader(lengths1);
  body_buffer_.OnDataPayload(body1);

  // 2nd frame.
  std::string body2(2048, 'b');
  const QuicByteCount header_length2 = 4;
  Http3FrameLengths lengths2(header_length2, 2048);
  body_buffer_.OnDataHeader(lengths2);
  body_buffer_.OnDataPayload(body2);

  EXPECT_EQ(header_length1 + 512, body_buffer_.OnBodyConsumed(512));
  EXPECT_EQ(header_length2 + 2048, body_buffer_.OnBodyConsumed(2048));
  EXPECT_EQ(512u, body_buffer_.OnBodyConsumed(512));
}

TEST_F(QuicSpdyStreamBodyBufferTest, MarkConsumedMoreThanBuffered) {
  std::string body(1024, 'a');
  Http3FrameLengths lengths(3, 1024);
  body_buffer_.OnDataHeader(lengths);
  body_buffer_.OnDataPayload(body);
  size_t bytes_to_consume = 0;
  EXPECT_QUIC_BUG(
      bytes_to_consume = body_buffer_.OnBodyConsumed(2048),
      "Invalid argument to OnBodyConsumed. expect to consume: 2048, but not "
      "enough bytes available. Total bytes readable are: 1024");
  EXPECT_EQ(0u, bytes_to_consume);
}

// Buffer receives 1 frame. Stream read from the buffer.
TEST_F(QuicSpdyStreamBodyBufferTest, ReadSingleBody) {
  testing::InSequence seq;
  std::string body(1024, 'a');
  const QuicByteCount header_length = 2;
  Http3FrameLengths lengths(header_length, 1024);
  body_buffer_.OnDataHeader(lengths);
  body_buffer_.OnDataPayload(body);

  char base[1024];
  iovec iov = {&base[0], 1024};
  size_t total_bytes_read = 0;
  EXPECT_EQ(header_length + 1024,
            body_buffer_.ReadBody(&iov, 1, &total_bytes_read));
  EXPECT_EQ(1024u, total_bytes_read);
  EXPECT_EQ(1024u, iov.iov_len);
  EXPECT_EQ(body,
            QuicStringPiece(static_cast<const char*>(iov.iov_base), 1024));
}

// Buffer receives 2 frames, stream read from the buffer multiple times.
TEST_F(QuicSpdyStreamBodyBufferTest, ReadMultipleBody) {
  testing::InSequence seq;
  // 1st frame.
  std::string body1(1024, 'a');
  const QuicByteCount header_length1 = 2;
  Http3FrameLengths lengths1(header_length1, 1024);
  body_buffer_.OnDataHeader(lengths1);
  body_buffer_.OnDataPayload(body1);

  // 2nd frame.
  std::string body2(2048, 'b');
  const QuicByteCount header_length2 = 4;
  Http3FrameLengths lengths2(header_length2, 2048);
  body_buffer_.OnDataHeader(lengths2);
  body_buffer_.OnDataPayload(body2);

  // First read of 512 bytes.
  char base[512];
  iovec iov = {&base[0], 512};
  size_t total_bytes_read = 0;
  EXPECT_EQ(header_length1 + 512,
            body_buffer_.ReadBody(&iov, 1, &total_bytes_read));
  EXPECT_EQ(512u, total_bytes_read);
  EXPECT_EQ(512u, iov.iov_len);
  EXPECT_EQ(body1.substr(0, 512),
            QuicStringPiece(static_cast<const char*>(iov.iov_base), 512));

  // Second read of 2048 bytes.
  char base2[2048];
  iovec iov2 = {&base2[0], 2048};
  EXPECT_EQ(header_length2 + 2048,
            body_buffer_.ReadBody(&iov2, 1, &total_bytes_read));
  EXPECT_EQ(2048u, total_bytes_read);
  EXPECT_EQ(2048u, iov2.iov_len);
  EXPECT_EQ(body1.substr(512, 512) + body2.substr(0, 1536),
            QuicStringPiece(static_cast<const char*>(iov2.iov_base), 2048));

  // Third read of the rest 512 bytes.
  char base3[512];
  iovec iov3 = {&base3[0], 512};
  EXPECT_EQ(512u, body_buffer_.ReadBody(&iov3, 1, &total_bytes_read));
  EXPECT_EQ(512u, total_bytes_read);
  EXPECT_EQ(512u, iov3.iov_len);
  EXPECT_EQ(body2.substr(1536, 512),
            QuicStringPiece(static_cast<const char*>(iov3.iov_base), 512));
}

}  // anonymous namespace

}  // namespace test

}  // namespace quic
