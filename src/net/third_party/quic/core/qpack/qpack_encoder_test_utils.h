// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_TEST_UTILS_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_TEST_UTILS_H_

#include "net/third_party/quic/core/qpack/qpack_encoder.h"
#include "net/third_party/quic/core/qpack/qpack_test_utils.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

namespace quic {
namespace test {

// QpackEncoder::DecoderStreamErrorDelegate implementation that does nothing.
class NoopDecoderStreamErrorDelegate
    : public QpackEncoder::DecoderStreamErrorDelegate {
 public:
  ~NoopDecoderStreamErrorDelegate() override = default;

  void OnError(QuicStringPiece error_message) override;
};

// Mock QpackEncoder::DecoderStreamErrorDelegate implementation.
class MockDecoderStreamErrorDelegate
    : public QpackEncoder::DecoderStreamErrorDelegate {
 public:
  ~MockDecoderStreamErrorDelegate() override = default;

  MOCK_METHOD1(OnError, void(QuicStringPiece error_message));
};

// QpackEncoderStreamSender::Delegate implementation that does nothing.
class NoopEncoderStreamSenderDelegate
    : public QpackEncoderStreamSender::Delegate {
 public:
  ~NoopEncoderStreamSenderDelegate() override = default;

  void Write(QuicStringPiece data) override;
};

QuicString QpackEncode(
    QpackEncoder::DecoderStreamErrorDelegate* decoder_stream_error_delegate,
    QpackEncoderStreamSender::Delegate* encoder_stream_sender_delegate,
    const FragmentSizeGenerator& fragment_size_generator,
    const spdy::SpdyHeaderBlock* header_list);

}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_TEST_UTILS_H_
