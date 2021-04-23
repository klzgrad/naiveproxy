// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_decoder_stream_sender.h"

#include "absl/strings/escaping.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/qpack/qpack_test_utils.h"
#include "common/platform/api/quiche_text_utils.h"

using ::testing::Eq;
using ::testing::StrictMock;

namespace quic {
namespace test {
namespace {

class QpackDecoderStreamSenderTest : public QuicTest {
 protected:
  QpackDecoderStreamSenderTest() {
    stream_.set_qpack_stream_sender_delegate(&delegate_);
  }
  ~QpackDecoderStreamSenderTest() override = default;

  StrictMock<MockQpackStreamSenderDelegate> delegate_;
  QpackDecoderStreamSender stream_;
};

TEST_F(QpackDecoderStreamSenderTest, InsertCountIncrement) {
  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("00"))));
  stream_.SendInsertCountIncrement(0);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("0a"))));
  stream_.SendInsertCountIncrement(10);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("3f00"))));
  stream_.SendInsertCountIncrement(63);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("3f8901"))));
  stream_.SendInsertCountIncrement(200);
  stream_.Flush();
}

TEST_F(QpackDecoderStreamSenderTest, HeaderAcknowledgement) {
  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("80"))));
  stream_.SendHeaderAcknowledgement(0);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("a5"))));
  stream_.SendHeaderAcknowledgement(37);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("ff00"))));
  stream_.SendHeaderAcknowledgement(127);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("fff802"))));
  stream_.SendHeaderAcknowledgement(503);
  stream_.Flush();
}

TEST_F(QpackDecoderStreamSenderTest, StreamCancellation) {
  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("40"))));
  stream_.SendStreamCancellation(0);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("53"))));
  stream_.SendStreamCancellation(19);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("7f00"))));
  stream_.SendStreamCancellation(63);
  stream_.Flush();

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("7f2f"))));
  stream_.SendStreamCancellation(110);
  stream_.Flush();
}

TEST_F(QpackDecoderStreamSenderTest, Coalesce) {
  stream_.SendInsertCountIncrement(10);
  stream_.SendHeaderAcknowledgement(37);
  stream_.SendStreamCancellation(0);

  EXPECT_CALL(delegate_, WriteStreamData(Eq(absl::HexStringToBytes("0aa540"))));
  stream_.Flush();

  stream_.SendInsertCountIncrement(63);
  stream_.SendStreamCancellation(110);

  EXPECT_CALL(delegate_,
              WriteStreamData(Eq(absl::HexStringToBytes("3f007f2f"))));
  stream_.Flush();
}

}  // namespace
}  // namespace test
}  // namespace quic
