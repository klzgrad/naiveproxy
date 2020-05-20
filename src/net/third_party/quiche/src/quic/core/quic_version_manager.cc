// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

namespace quic {

QuicVersionManager::QuicVersionManager(
    ParsedQuicVersionVector supported_versions)
    : enable_version_draft_27_(
          GetQuicReloadableFlag(quic_enable_version_draft_27)),
      enable_version_draft_25_(
          GetQuicReloadableFlag(quic_enable_version_draft_25_v3)),
      disable_version_q050_(GetQuicReloadableFlag(quic_disable_version_q050)),
      enable_version_t050_(GetQuicReloadableFlag(quic_enable_version_t050)),
      disable_version_q049_(GetQuicReloadableFlag(quic_disable_version_q049)),
      disable_version_q048_(GetQuicReloadableFlag(quic_disable_version_q048)),
      disable_version_q046_(GetQuicReloadableFlag(quic_disable_version_q046)),
      disable_version_q043_(GetQuicReloadableFlag(quic_disable_version_q043)),
      allowed_supported_versions_(std::move(supported_versions)) {
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  RefilterSupportedVersions();
}

QuicVersionManager::~QuicVersionManager() {}

const QuicTransportVersionVector&
QuicVersionManager::GetSupportedTransportVersions() {
  MaybeRefilterSupportedVersions();
  return filtered_transport_versions_;
}

const ParsedQuicVersionVector& QuicVersionManager::GetSupportedVersions() {
  MaybeRefilterSupportedVersions();
  return filtered_supported_versions_;
}

void QuicVersionManager::MaybeRefilterSupportedVersions() {
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  if (enable_version_draft_27_ !=
          GetQuicReloadableFlag(quic_enable_version_draft_27) ||
      enable_version_draft_25_ !=
          GetQuicReloadableFlag(quic_enable_version_draft_25_v3) ||
      disable_version_q050_ !=
          GetQuicReloadableFlag(quic_disable_version_q050) ||
      enable_version_t050_ != GetQuicReloadableFlag(quic_enable_version_t050) ||
      disable_version_q049_ !=
          GetQuicReloadableFlag(quic_disable_version_q049) ||
      disable_version_q048_ !=
          GetQuicReloadableFlag(quic_disable_version_q048) ||
      disable_version_q046_ !=
          GetQuicReloadableFlag(quic_disable_version_q046) ||
      disable_version_q043_ !=
          GetQuicReloadableFlag(quic_disable_version_q043)) {
    enable_version_draft_27_ =
        GetQuicReloadableFlag(quic_enable_version_draft_27);
    enable_version_draft_25_ =
        GetQuicReloadableFlag(quic_enable_version_draft_25_v3);
    disable_version_q050_ = GetQuicReloadableFlag(quic_disable_version_q050);
    enable_version_t050_ = GetQuicReloadableFlag(quic_enable_version_t050);
    disable_version_q049_ = GetQuicReloadableFlag(quic_disable_version_q049);
    disable_version_q048_ = GetQuicReloadableFlag(quic_disable_version_q048);
    disable_version_q046_ = GetQuicReloadableFlag(quic_disable_version_q046);
    disable_version_q043_ = GetQuicReloadableFlag(quic_disable_version_q043);

    RefilterSupportedVersions();
  }
}

void QuicVersionManager::RefilterSupportedVersions() {
  filtered_supported_versions_ =
      FilterSupportedVersions(allowed_supported_versions_);
  filtered_transport_versions_.clear();
  for (ParsedQuicVersion version : filtered_supported_versions_) {
    auto transport_version = version.transport_version;
    if (std::find(filtered_transport_versions_.begin(),
                  filtered_transport_versions_.end(),
                  transport_version) == filtered_transport_versions_.end()) {
      filtered_transport_versions_.push_back(transport_version);
    }
  }
}

}  // namespace quic
