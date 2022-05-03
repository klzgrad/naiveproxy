// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/test_tools/qpack/qpack_encoder_test_utils.h"

#include "absl/strings/string_view.h"
#include "spdy/core/hpack/hpack_encoder.h"

namespace quic {
namespace test {

void NoopDecoderStreamErrorDelegate::OnDecoderStreamError(
    QuicErrorCode /*error_code*/,
    absl::string_view /*error_message*/) {}

}  // namespace test
}  // namespace quic
