// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_ENCODER_TEST_UTILS_H_
#define QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_ENCODER_TEST_UTILS_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

namespace quic {
namespace test {

// QpackEncoder::DecoderStreamErrorDelegate implementation that does nothing.
class NoopDecoderStreamErrorDelegate
    : public QpackEncoder::DecoderStreamErrorDelegate {
 public:
  ~NoopDecoderStreamErrorDelegate() override = default;

  void OnDecoderStreamError(quiche::QuicheStringPiece error_message) override;
};

// Mock QpackEncoder::DecoderStreamErrorDelegate implementation.
class MockDecoderStreamErrorDelegate
    : public QpackEncoder::DecoderStreamErrorDelegate {
 public:
  ~MockDecoderStreamErrorDelegate() override = default;

  MOCK_METHOD1(OnDecoderStreamError,
               void(quiche::QuicheStringPiece error_message));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_ENCODER_TEST_UTILS_H_
