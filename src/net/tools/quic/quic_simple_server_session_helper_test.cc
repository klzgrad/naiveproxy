// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_connection_id.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/test_tools/mock_random.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quic/tools/quic_simple_crypto_server_stream_helper.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TEST(QuicSimpleCryptoServerStreamHelperTest, GenerateConnectionIdForReject) {
  quic::test::MockRandom random;
  quic::QuicSimpleCryptoServerStreamHelper helper(&random);

  EXPECT_EQ(
      quic::QuicUtils::CreateRandomConnectionId(&random),
      helper.GenerateConnectionIdForReject(quic::test::TestConnectionId(42)));
}

}  // namespace net
