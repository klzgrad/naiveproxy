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

TEST(QuicUtilsChromiumTest, ParseQuicVersions) {
  EXPECT_THAT(ParseQuicVersions(""), IsEmpty());

  quic::ParsedQuicVersion expected_version1 = {quic::PROTOCOL_QUIC_CRYPTO,
                                               quic::QUIC_VERSION_50};
  EXPECT_THAT(ParseQuicVersions("QUIC_VERSION_50"),
              ElementsAre(expected_version1));
  EXPECT_THAT(ParseQuicVersions("h3-Q050"), ElementsAre(expected_version1));

  quic::ParsedQuicVersion expected_version2 = {quic::PROTOCOL_TLS1_3,
                                               quic::QUIC_VERSION_50};
  EXPECT_THAT(ParseQuicVersions("h3-T050"), ElementsAre(expected_version2));

  EXPECT_THAT(ParseQuicVersions("h3-Q050, h3-T050"),
              ElementsAre(expected_version1, expected_version2));
  EXPECT_THAT(ParseQuicVersions("h3-T050, h3-Q050"),
              ElementsAre(expected_version2, expected_version1));
  EXPECT_THAT(ParseQuicVersions("QUIC_VERSION_50,h3-T050"),
              ElementsAre(expected_version1, expected_version2));
  EXPECT_THAT(ParseQuicVersions("h3-T050,QUIC_VERSION_50"),
              ElementsAre(expected_version2, expected_version1));
  EXPECT_THAT(ParseQuicVersions("QUIC_VERSION_50, h3-T050"),
              ElementsAre(expected_version1, expected_version2));
  EXPECT_THAT(ParseQuicVersions("h3-T050, QUIC_VERSION_50"),
              ElementsAre(expected_version2, expected_version1));

  quic::ParsedQuicVersion expected_version3 = {quic::PROTOCOL_QUIC_CRYPTO,
                                               quic::QUIC_VERSION_49};
  EXPECT_THAT(ParseQuicVersions("QUIC_VERSION_50,QUIC_VERSION_49"),
              ElementsAre(expected_version1, expected_version3));
  EXPECT_THAT(ParseQuicVersions("QUIC_VERSION_49,QUIC_VERSION_50"),
              ElementsAre(expected_version3, expected_version1));

  // Regression test for https://crbug.com/1044952.
  EXPECT_THAT(ParseQuicVersions("QUIC_VERSION_50, QUIC_VERSION_50"),
              ElementsAre(expected_version1));
  EXPECT_THAT(ParseQuicVersions("h3-Q050, h3-Q050"),
              ElementsAre(expected_version1));
  EXPECT_THAT(ParseQuicVersions("h3-T050, h3-T050"),
              ElementsAre(expected_version2));
  EXPECT_THAT(ParseQuicVersions("h3-Q050, QUIC_VERSION_50"),
              ElementsAre(expected_version1));
  EXPECT_THAT(
      ParseQuicVersions("QUIC_VERSION_50, h3-Q050, QUIC_VERSION_50, h3-Q050"),
      ElementsAre(expected_version1));
  EXPECT_THAT(ParseQuicVersions("QUIC_VERSION_50, h3-T050, h3-Q050"),
              ElementsAre(expected_version1, expected_version2));
}

}  // namespace
}  // namespace test
}  // namespace net
