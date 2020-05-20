// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_utils_chromium.h"

#include <map>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace net {
namespace test {
namespace {

TEST(QuicUtilsChromiumTest, ParseQuicConnectionOptions) {
  quic::QuicTagVector empty_options = ParseQuicConnectionOptions("");
  EXPECT_TRUE(empty_options.empty());

  quic::QuicTagVector parsed_options =
      ParseQuicConnectionOptions("TIMER,TBBR,REJ");
  quic::QuicTagVector expected_options;
  expected_options.push_back(quic::kTIME);
  expected_options.push_back(quic::kTBBR);
  expected_options.push_back(quic::kREJ);
  EXPECT_EQ(expected_options, parsed_options);
}

}  // namespace
}  // namespace test
}  // namespace net
