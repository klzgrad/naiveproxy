// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicErrorCodesTest : public QuicTest {};

TEST_F(QuicErrorCodesTest, QuicRstStreamErrorCodeToString) {
  EXPECT_STREQ("QUIC_BAD_APPLICATION_PAYLOAD",
               QuicRstStreamErrorCodeToString(QUIC_BAD_APPLICATION_PAYLOAD));
}

TEST_F(QuicErrorCodesTest, QuicErrorCodeToString) {
  EXPECT_STREQ("QUIC_NO_ERROR", QuicErrorCodeToString(QUIC_NO_ERROR));
}

}  // namespace
}  // namespace test
}  // namespace quic
