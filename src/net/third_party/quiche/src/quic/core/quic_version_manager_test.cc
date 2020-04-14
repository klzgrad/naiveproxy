// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

namespace quic {
namespace test {
namespace {

class QuicVersionManagerTest : public QuicTest {};

TEST_F(QuicVersionManagerTest, QuicVersionManager) {
  static_assert(QUICHE_ARRAYSIZE(kSupportedTransportVersions) == 6u,
                "Supported versions out of sync");
  SetQuicReloadableFlag(quic_enable_version_t099, false);
  SetQuicReloadableFlag(quic_enable_version_t050, false);
  SetQuicReloadableFlag(quic_disable_version_q050, false);
  SetQuicReloadableFlag(quic_disable_version_q049, false);
  SetQuicReloadableFlag(quic_disable_version_q048, false);
  SetQuicReloadableFlag(quic_disable_version_q046, false);
  SetQuicReloadableFlag(quic_disable_version_q043, false);
  QuicVersionManager manager(AllSupportedVersions());

  ParsedQuicVersionVector expected_parsed_versions;
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_49));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46));
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43));

  EXPECT_EQ(expected_parsed_versions, manager.GetSupportedVersions());

  EXPECT_EQ(FilterSupportedVersions(AllSupportedVersions()),
            manager.GetSupportedVersions());

  SetQuicReloadableFlag(quic_enable_version_t099, true);
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_99));
  EXPECT_EQ(expected_parsed_versions, manager.GetSupportedVersions());
  EXPECT_EQ(FilterSupportedVersions(AllSupportedVersions()),
            manager.GetSupportedVersions());

  SetQuicReloadableFlag(quic_enable_version_t050, true);
  expected_parsed_versions.push_back(
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50));
  EXPECT_EQ(expected_parsed_versions, manager.GetSupportedVersions());
  EXPECT_EQ(FilterSupportedVersions(AllSupportedVersions()),
            manager.GetSupportedVersions());
}

}  // namespace
}  // namespace test
}  // namespace quic
