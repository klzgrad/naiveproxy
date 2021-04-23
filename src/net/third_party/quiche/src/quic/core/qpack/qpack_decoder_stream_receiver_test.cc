// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_decoder_stream_receiver.h"

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_test.h"
#include "common/platform/api/quiche_text_utils.h"

using testing::Eq;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

class MockDelegate : public QpackDecoderStreamReceiver::Delegate {
 public:
  ~MockDelegate() override = default;

  MOCK_METHOD(void, OnInsertCountIncrement, (uint64_t increment), (override));
  MOCK_METHOD(void,
              OnHeaderAcknowledgement,
              (QuicStreamId stream_id),
              (override));
  MOCK_METHOD(void, OnStreamCancellation, (QuicStreamId stream_id), (override));
  MOCK_METHOD(void,
              OnErrorDetected,
              (QuicErrorCode error_code, absl::string_view error_message),
              (override));
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
  stream_.Decode(absl::HexStringToBytes("00"));

  EXPECT_CALL(delegate_, OnInsertCountIncrement(10));
  stream_.Decode(absl::HexStringToBytes("0a"));

  EXPECT_CALL(delegate_, OnInsertCountIncrement(63));
  stream_.Decode(absl::HexStringToBytes("3f00"));

  EXPECT_CALL(delegate_, OnInsertCountIncrement(200));
  stream_.Decode(absl::HexStringToBytes("3f8901"));

  EXPECT_CALL(delegate_,
              OnErrorDetected(QUIC_QPACK_DECODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));
  stream_.Decode(absl::HexStringToBytes("3fffffffffffffffffffff"));
}

TEST_F(QpackDecoderStreamReceiverTest, HeaderAcknowledgement) {
  EXPECT_CALL(delegate_, OnHeaderAcknowledgement(0));
  stream_.Decode(absl::HexStringToBytes("80"));

  EXPECT_CALL(delegate_, OnHeaderAcknowledgement(37));
  stream_.Decode(absl::HexStringToBytes("a5"));

  EXPECT_CALL(delegate_, OnHeaderAcknowledgement(127));
  stream_.Decode(absl::HexStringToBytes("ff00"));

  EXPECT_CALL(delegate_, OnHeaderAcknowledgement(503));
  stream_.Decode(absl::HexStringToBytes("fff802"));

  EXPECT_CALL(delegate_,
              OnErrorDetected(QUIC_QPACK_DECODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));
  stream_.Decode(absl::HexStringToBytes("ffffffffffffffffffffff"));
}

TEST_F(QpackDecoderStreamReceiverTest, StreamCancellation) {
  EXPECT_CALL(delegate_, OnStreamCancellation(0));
  stream_.Decode(absl::HexStringToBytes("40"));

  EXPECT_CALL(delegate_, OnStreamCancellation(19));
  stream_.Decode(absl::HexStringToBytes("53"));

  EXPECT_CALL(delegate_, OnStreamCancellation(63));
  stream_.Decode(absl::HexStringToBytes("7f00"));

  EXPECT_CALL(delegate_, OnStreamCancellation(110));
  stream_.Decode(absl::HexStringToBytes("7f2f"));

  EXPECT_CALL(delegate_,
              OnErrorDetected(QUIC_QPACK_DECODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));
  stream_.Decode(absl::HexStringToBytes("7fffffffffffffffffffff"));
}

}  // namespace
}  // namespace test
}  // namespace quic
