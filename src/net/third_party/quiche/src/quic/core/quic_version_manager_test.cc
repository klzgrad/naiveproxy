// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicVersionManagerTest : public QuicTest {};

TEST_F(QuicVersionManagerTest, QuicVersionManager) {
  static_assert(QUIC_ARRAYSIZE(kSupportedTransportVersions) == 6u,
                "Supported versions out of sync");
  SetQuicReloadableFlag(quic_enable_version_99, false);
  SetQuicReloadableFlag(quic_enable_version_48_2, false);
  SetQuicReloadableFlag(quic_enable_version_47, false);
  SetQuicReloadableFlag(quic_disable_version_39, true);
  QuicVersionManager manager(AllSupportedVersions());

  EXPECT_EQ(FilterSupportedTransportVersions(AllSupportedTransportVersions()),
            manager.GetSupportedTransportVersions());

  EXPECT_EQ(QuicTransportVersionVector({QUIC_VERSION_46, QUIC_VERSION_43}),
            manager.GetSupportedTransportVersions());

  SetQuicReloadableFlag(quic_disable_version_39, false);
  EXPECT_EQ(QuicTransportVersionVector(
                {QUIC_VERSION_46, QUIC_VERSION_43, QUIC_VERSION_39}),
            manager.GetSupportedTransportVersions());

  SetQuicReloadableFlag(quic_enable_version_47, true);
  EXPECT_EQ(QuicTransportVersionVector({QUIC_VERSION_47, QUIC_VERSION_46,
                                        QUIC_VERSION_43, QUIC_VERSION_39}),
            manager.GetSupportedTransportVersions());

  SetQuicReloadableFlag(quic_enable_version_48_2, true);
  EXPECT_EQ(QuicTransportVersionVector({QUIC_VERSION_48, QUIC_VERSION_47,
                                        QUIC_VERSION_46, QUIC_VERSION_43,
                                        QUIC_VERSION_39}),
            manager.GetSupportedTransportVersions());

  SetQuicReloadableFlag(quic_enable_version_99, true);
  SetQuicReloadableFlag(quic_use_parse_public_header, true);
  EXPECT_EQ(QuicTransportVersionVector({QUIC_VERSION_99, QUIC_VERSION_48,
                                        QUIC_VERSION_47, QUIC_VERSION_46,
                                        QUIC_VERSION_43, QUIC_VERSION_39}),
            manager.GetSupportedTransportVersions());

  // Ensure that all versions are now supported.
  EXPECT_EQ(FilterSupportedTransportVersions(AllSupportedTransportVersions()),
            manager.GetSupportedTransportVersions());
}

}  // namespace
}  // namespace test
}  // namespace quic
