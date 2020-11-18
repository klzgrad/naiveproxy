// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

using ::testing::ElementsAre;

namespace quic {
namespace test {
namespace {

class QuicVersionManagerTest : public QuicTest {};

TEST_F(QuicVersionManagerTest, QuicVersionManager) {
  static_assert(SupportedVersions().size() == 7u,
                "Supported versions out of sync");
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    QuicEnableVersion(version);
  }
  QuicDisableVersion(ParsedQuicVersion::Draft29());
  QuicDisableVersion(ParsedQuicVersion::Draft27());
  QuicVersionManager manager(AllSupportedVersions());

  ParsedQuicVersionVector expected_parsed_versions;
  expected_parsed_versions.push_back(ParsedQuicVersion::T051());
  expected_parsed_versions.push_back(ParsedQuicVersion::T050());
  expected_parsed_versions.push_back(ParsedQuicVersion::Q050());
  expected_parsed_versions.push_back(ParsedQuicVersion::Q046());
  expected_parsed_versions.push_back(ParsedQuicVersion::Q043());

  EXPECT_EQ(expected_parsed_versions, manager.GetSupportedVersions());

  EXPECT_EQ(FilterSupportedVersions(AllSupportedVersions()),
            manager.GetSupportedVersions());
  EXPECT_EQ(CurrentSupportedVersionsWithQuicCrypto(),
            manager.GetSupportedVersionsWithQuicCrypto());
  EXPECT_THAT(
      manager.GetSupportedAlpns(),
      ElementsAre("h3-T051", "h3-T050", "h3-Q050", "h3-Q046", "h3-Q043"));

  int offset = 0;
  QuicEnableVersion(ParsedQuicVersion::Draft29());
  expected_parsed_versions.insert(expected_parsed_versions.begin() + offset,
                                  ParsedQuicVersion::Draft29());
  EXPECT_EQ(expected_parsed_versions, manager.GetSupportedVersions());
  EXPECT_EQ(expected_parsed_versions.size() - 3 - offset,
            manager.GetSupportedVersionsWithQuicCrypto().size());
  EXPECT_EQ(FilterSupportedVersions(AllSupportedVersions()),
            manager.GetSupportedVersions());
  EXPECT_EQ(CurrentSupportedVersionsWithQuicCrypto(),
            manager.GetSupportedVersionsWithQuicCrypto());
  EXPECT_THAT(manager.GetSupportedAlpns(),
              ElementsAre("h3-29", "h3-T051", "h3-T050", "h3-Q050", "h3-Q046",
                          "h3-Q043"));

  offset++;
  QuicEnableVersion(ParsedQuicVersion::Draft27());
  expected_parsed_versions.insert(expected_parsed_versions.begin() + offset,
                                  ParsedQuicVersion::Draft27());
  EXPECT_EQ(expected_parsed_versions, manager.GetSupportedVersions());
  EXPECT_EQ(expected_parsed_versions.size() - 3 - offset,
            manager.GetSupportedVersionsWithQuicCrypto().size());
  EXPECT_EQ(FilterSupportedVersions(AllSupportedVersions()),
            manager.GetSupportedVersions());
  EXPECT_EQ(CurrentSupportedVersionsWithQuicCrypto(),
            manager.GetSupportedVersionsWithQuicCrypto());
  EXPECT_THAT(manager.GetSupportedAlpns(),
              ElementsAre("h3-29", "h3-27", "h3-T051", "h3-T050", "h3-Q050",
                          "h3-Q046", "h3-Q043"));
}

}  // namespace
}  // namespace test
}  // namespace quic
