// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/http/quic_spdy_stream_body_buffer.h"

#include "net/third_party/quic/core/quic_stream_sequencer.h"
#include "net/third_party/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace quic {

namespace test {

namespace {

class MockStream : public QuicStreamSequencer::StreamInterface {
 public:
  MOCK_METHOD0(OnFinRead, void());
  MOCK_METHOD0(OnDataAvailable, void());
  MOCK_METHOD2(CloseConnectionWithDetails,
               void(QuicErrorCode error, const QuicString& details));
  MOCK_METHOD1(Reset, void(QuicRstStreamErrorCode error));
  MOCK_METHOD0(OnCanWrite, void());
  MOCK_METHOD1(AddBytesConsumed, void(QuicByteCount bytes));

  QuicStreamId id() const override { return 1; }

  const QuicSocketAddress& PeerAddressOfLatestPacket() const override {
    return peer_address_;
  }

 protected:
  QuicSocketAddress peer_address_ =
      QuicSocketAddress(QuicIpAddress::Any4(), 65535);
};

class MockSequencer : public QuicStreamSequencer {
 public:
  explicit MockSequencer(MockStream* stream) : QuicStreamSequencer(stream) {}
  virtual ~MockSequencer() = default;
  MOCK_METHOD1(MarkConsumed, void(size_t num_bytes_consumed));
};

class QuicSpdyStreamBodyBufferTest : public QuicTest {
 public:
  QuicSpdyStreamBodyBufferTest()
      : sequencer_(&stream_), body_buffer_(&sequencer_) {}

 protected:
  MockStream stream_;
  MockSequencer sequencer_;
  QuicSpdyStreamBodyBuffer body_buffer_;
  HttpEncoder encoder_;
};

TEST_F(QuicSpdyStreamBodyBufferTest, ReceiveBodies) {
  QuicString body(1024, 'a');
  EXPECT_FALSE(body_buffer_.HasBytesToRead());
  body_buffer_.OnDataHeader(Http3FrameLengths(3, 1024));
  body_buffer_.OnDataPayload(QuicStringPiece(body));
  EXPECT_EQ(1024u, body_buffer_.total_body_bytes_received());
  EXPECT_TRUE(body_buffer_.HasBytesToRead());
}

TEST_F(QuicSpdyStreamBodyBufferTest, PeekBody) {
  QuicString body(1024, 'a');
  body_buffer_.OnDataHeader(Http3FrameLengths(3, 1024));
  body_buffer_.OnDataPayload(QuicStringPiece(body));
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
  QuicString body(1024, 'a');
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(body.length(), &buffer);
  QuicString header = QuicString(buffer.get(), header_length);
  Http3FrameLengths lengths(header_length, 1024);
  QuicString data = header + body;
  QuicStreamFrame frame(1, false, 0, data);
  sequencer_.OnStreamFrame(frame);
  body_buffer_.OnDataHeader(lengths);
  body_buffer_.OnDataPayload(QuicStringPiece(body));
  EXPECT_CALL(stream_, AddBytesConsumed(header_length));
  EXPECT_CALL(stream_, AddBytesConsumed(1024));
  body_buffer_.MarkBodyConsumed(1024);
}

// Buffer received 2 frames. Stream consumes multiple times.
TEST_F(QuicSpdyStreamBodyBufferTest, MarkConsumedMultipleFrames) {
  testing::InSequence seq;
  // 1st frame.
  QuicString body1(1024, 'a');
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length1 =
      encoder_.SerializeDataFrameHeader(body1.length(), &buffer);
  QuicString header1 = QuicString(buffer.get(), header_length1);
  Http3FrameLengths lengths1(header_length1, 1024);
  QuicString data1 = header1 + body1;
  QuicStreamFrame frame1(1, false, 0, data1);
  sequencer_.OnStreamFrame(frame1);
  body_buffer_.OnDataHeader(lengths1);
  body_buffer_.OnDataPayload(QuicStringPiece(body1));

  // 2nd frame.
  QuicString body2(2048, 'b');
  QuicByteCount header_length2 =
      encoder_.SerializeDataFrameHeader(body2.length(), &buffer);
  QuicString header2 = QuicString(buffer.get(), header_length2);
  Http3FrameLengths lengths2(header_length2, 2048);
  QuicString data2 = header2 + body2;
  QuicStreamFrame frame2(1, false, data1.length(), data2);
  sequencer_.OnStreamFrame(frame2);
  body_buffer_.OnDataHeader(lengths2);
  body_buffer_.OnDataPayload(QuicStringPiece(body2));

  EXPECT_CALL(stream_, AddBytesConsumed(header_length1));
  EXPECT_CALL(stream_, AddBytesConsumed(512));
  body_buffer_.MarkBodyConsumed(512);
  EXPECT_CALL(stream_, AddBytesConsumed(header_length2));
  EXPECT_CALL(stream_, AddBytesConsumed(2048));
  body_buffer_.MarkBodyConsumed(2048);
  EXPECT_CALL(stream_, AddBytesConsumed(512));
  body_buffer_.MarkBodyConsumed(512);
}

TEST_F(QuicSpdyStreamBodyBufferTest, MarkConsumedMoreThanBuffered) {
  QuicString body(1024, 'a');
  Http3FrameLengths lengths(3, 1024);
  body_buffer_.OnDataHeader(lengths);
  body_buffer_.OnDataPayload(body);
  EXPECT_QUIC_BUG(
      body_buffer_.MarkBodyConsumed(2048),
      "Invalid argument to MarkBodyConsumed. expect to consume: 2048, but not "
      "enough bytes available. Total bytes readable are: 1024");
}

// Buffer receives 1 frame. Stream read from the buffer.
TEST_F(QuicSpdyStreamBodyBufferTest, ReadSingleBody) {
  testing::InSequence seq;
  QuicString body(1024, 'a');
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(body.length(), &buffer);
  QuicString header = QuicString(buffer.get(), header_length);
  Http3FrameLengths lengths(header_length, 1024);
  QuicString data = header + body;
  QuicStreamFrame frame(1, false, 0, data);
  sequencer_.OnStreamFrame(frame);
  body_buffer_.OnDataHeader(lengths);
  body_buffer_.OnDataPayload(QuicStringPiece(body));

  EXPECT_CALL(stream_, AddBytesConsumed(header_length));
  EXPECT_CALL(stream_, AddBytesConsumed(1024));

  char base[1024];
  iovec iov = {&base[0], 1024};
  EXPECT_EQ(1024u, body_buffer_.ReadBody(&iov, 1));
  EXPECT_EQ(1024u, iov.iov_len);
  EXPECT_EQ(body,
            QuicStringPiece(static_cast<const char*>(iov.iov_base), 1024));
}

// Buffer receives 2 frames, stream read from the buffer multiple times.
TEST_F(QuicSpdyStreamBodyBufferTest, ReadMultipleBody) {
  testing::InSequence seq;
  // 1st frame.
  QuicString body1(1024, 'a');
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length1 =
      encoder_.SerializeDataFrameHeader(body1.length(), &buffer);
  QuicString header1 = QuicString(buffer.get(), header_length1);
  Http3FrameLengths lengths1(header_length1, 1024);
  QuicString data1 = header1 + body1;
  QuicStreamFrame frame1(1, false, 0, data1);
  sequencer_.OnStreamFrame(frame1);
  body_buffer_.OnDataHeader(lengths1);
  body_buffer_.OnDataPayload(QuicStringPiece(body1));

  // 2nd frame.
  QuicString body2(2048, 'b');
  QuicByteCount header_length2 =
      encoder_.SerializeDataFrameHeader(body2.length(), &buffer);
  QuicString header2 = QuicString(buffer.get(), header_length2);
  Http3FrameLengths lengths2(header_length2, 2048);
  QuicString data2 = header2 + body2;
  QuicStreamFrame frame2(1, false, data1.length(), data2);
  sequencer_.OnStreamFrame(frame2);
  body_buffer_.OnDataHeader(lengths2);
  body_buffer_.OnDataPayload(QuicStringPiece(body2));

  // First read of 512 bytes.
  EXPECT_CALL(stream_, AddBytesConsumed(header_length1));
  EXPECT_CALL(stream_, AddBytesConsumed(512));
  char base[512];
  iovec iov = {&base[0], 512};
  EXPECT_EQ(512u, body_buffer_.ReadBody(&iov, 1));
  EXPECT_EQ(512u, iov.iov_len);
  EXPECT_EQ(body1.substr(0, 512),
            QuicStringPiece(static_cast<const char*>(iov.iov_base), 512));

  // Second read of 2048 bytes.
  EXPECT_CALL(stream_, AddBytesConsumed(header_length2));
  EXPECT_CALL(stream_, AddBytesConsumed(2048));
  char base2[2048];
  iovec iov2 = {&base2[0], 2048};
  EXPECT_EQ(2048u, body_buffer_.ReadBody(&iov2, 1));
  EXPECT_EQ(2048u, iov2.iov_len);
  EXPECT_EQ(body1.substr(512, 512) + body2.substr(0, 1536),
            QuicStringPiece(static_cast<const char*>(iov2.iov_base), 2048));

  // Third read of the rest 512 bytes.
  EXPECT_CALL(stream_, AddBytesConsumed(512));
  char base3[512];
  iovec iov3 = {&base3[0], 512};
  EXPECT_EQ(512u, body_buffer_.ReadBody(&iov3, 1));
  EXPECT_EQ(512u, iov3.iov_len);
  EXPECT_EQ(body2.substr(1536, 512),
            QuicStringPiece(static_cast<const char*>(iov3.iov_base), 512));
}

}  // anonymous namespace

}  // namespace test

}  // namespace quic
