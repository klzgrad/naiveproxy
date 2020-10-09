// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_encoder_test_utils.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_encoder.h"

namespace quic {
namespace test {

void NoopDecoderStreamErrorDelegate::OnDecoderStreamError(
    quiche::QuicheStringPiece /*error_message*/) {}

}  // namespace test
}  // namespace quic
