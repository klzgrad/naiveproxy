// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_stream_receiver.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

using testing::Eq;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

class MockDelegate : public QpackDecoderStreamReceiver::Delegate {
 public:
  ~MockDelegate() override = default;

  MOCK_METHOD1(OnInsertCountIncrement, void(uint64_t increment));
  MOCK_METHOD1(OnHeaderAcknowledgement, void(QuicStreamId stream_id));
  MOCK_METHOD1(OnStreamCancellation, void(QuicStreamId stream_id));
  MOCK_METHOD1(OnErrorDetected, void(quiche::QuicheStringPiece error_message));
};

class QpackDecoderStreamReceiverTest : public QuicTest {
 protected:
  QpackDecoderStreamReceiverTest() : stream_(&delegate_) {}
  ~QpackDecoderStreamReceiverTest() override = default;

  QpackDecoderStreamReceiver stream_;
  StrictMock<MockDelegate> delegate_;
};

TEST_F(QpackDecoderStreamReceiverTest, InsertCountIncrement) {
  EXPECT_CALL(delegate_, OnInsertCountIncrement(0));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("00"));

  EXPECT_CALL(delegate_, OnInsertCountIncrement(10));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("0a"));

  EXPECT_CALL(delegate_, OnInsertCountIncrement(63));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("3f00"));

  EXPECT_CALL(delegate_, OnInsertCountIncrement(200));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("3f8901"));

  EXPECT_CALL(delegate_, OnErrorDetected(Eq("Encoded integer too large.")));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("3fffffffffffffffffffff"));
}

TEST_F(QpackDecoderStreamReceiverTest, HeaderAcknowledgement) {
  EXPECT_CALL(delegate_, OnHeaderAcknowledgement(0));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("80"));

  EXPECT_CALL(delegate_, OnHeaderAcknowledgement(37));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("a5"));

  EXPECT_CALL(delegate_, OnHeaderAcknowledgement(127));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("ff00"));

  EXPECT_CALL(delegate_, OnHeaderAcknowledgement(503));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("fff802"));

  EXPECT_CALL(delegate_, OnErrorDetected(Eq("Encoded integer too large.")));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("ffffffffffffffffffffff"));
}

TEST_F(QpackDecoderStreamReceiverTest, StreamCancellation) {
  EXPECT_CALL(delegate_, OnStreamCancellation(0));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("40"));

  EXPECT_CALL(delegate_, OnStreamCancellation(19));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("53"));

  EXPECT_CALL(delegate_, OnStreamCancellation(63));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("7f00"));

  EXPECT_CALL(delegate_, OnStreamCancellation(110));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("7f2f"));

  EXPECT_CALL(delegate_, OnErrorDetected(Eq("Encoded integer too large.")));
  stream_.Decode(quiche::QuicheTextUtils::HexDecode("7fffffffffffffffffffff"));
}

}  // namespace
}  // namespace test
}  // namespace quic
