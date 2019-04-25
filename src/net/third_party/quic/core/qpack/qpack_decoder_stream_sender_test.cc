// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_decoder_stream_sender.h"

#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::Eq;
using ::testing::StrictMock;

namespace quic {
namespace test {
namespace {

class MockSenderDelegate : public QpackDecoderStreamSender::Delegate {
 public:
  ~MockSenderDelegate() override = default;

  MOCK_METHOD1(WriteDecoderStreamData, void(QuicStringPiece data));
};

class QpackDecoderStreamSenderTest : public QuicTest {
 protected:
  QpackDecoderStreamSenderTest() : stream_(&delegate_) {}
  ~QpackDecoderStreamSenderTest() override = default;

  StrictMock<MockSenderDelegate> delegate_;
  QpackDecoderStreamSender stream_;
};

TEST_F(QpackDecoderStreamSenderTest, InsertCountIncrement) {
  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("00"))));
  stream_.SendInsertCountIncrement(0);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("0a"))));
  stream_.SendInsertCountIncrement(10);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("3f00"))));
  stream_.SendInsertCountIncrement(63);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("3f8901"))));
  stream_.SendInsertCountIncrement(200);
}

TEST_F(QpackDecoderStreamSenderTest, HeaderAcknowledgement) {
  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("80"))));
  stream_.SendHeaderAcknowledgement(0);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("a5"))));
  stream_.SendHeaderAcknowledgement(37);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("ff00"))));
  stream_.SendHeaderAcknowledgement(127);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("fff802"))));
  stream_.SendHeaderAcknowledgement(503);
}

TEST_F(QpackDecoderStreamSenderTest, StreamCancellation) {
  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("40"))));
  stream_.SendStreamCancellation(0);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("53"))));
  stream_.SendStreamCancellation(19);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("7f00"))));
  stream_.SendStreamCancellation(63);

  EXPECT_CALL(delegate_,
              WriteDecoderStreamData(Eq(QuicTextUtils::HexDecode("7f2f"))));
  stream_.SendStreamCancellation(110);
}

}  // namespace
}  // namespace test
}  // namespace quic
