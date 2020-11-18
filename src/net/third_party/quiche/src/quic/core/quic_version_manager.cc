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
    : disable_version_draft_29_(
          GetQuicReloadableFlag(quic_disable_version_draft_29)),
      disable_version_draft_27_(
          GetQuicReloadableFlag(quic_disable_version_draft_27)),
      disable_version_t051_(GetQuicReloadableFlag(quic_disable_version_t051)),
      disable_version_t050_(GetQuicReloadableFlag(quic_disable_version_t050)),
      disable_version_q050_(GetQuicReloadableFlag(quic_disable_version_q050)),
      disable_version_q046_(GetQuicReloadableFlag(quic_disable_version_q046)),
      disable_version_q043_(GetQuicReloadableFlag(quic_disable_version_q043)),
      allowed_supported_versions_(std::move(supported_versions)) {
  static_assert(SupportedVersions().size() == 7u,
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

const ParsedQuicVersionVector&
QuicVersionManager::GetSupportedVersionsWithQuicCrypto() {
  MaybeRefilterSupportedVersions();
  return filtered_supported_versions_with_quic_crypto_;
}

const std::vector<std::string>& QuicVersionManager::GetSupportedAlpns() {
  MaybeRefilterSupportedVersions();
  return filtered_supported_alpns_;
}

void QuicVersionManager::MaybeRefilterSupportedVersions() {
  static_assert(SupportedVersions().size() == 7u,
                "Supported versions out of sync");
  if (disable_version_draft_29_ !=
          GetQuicReloadableFlag(quic_disable_version_draft_29) ||
      disable_version_draft_27_ !=
          GetQuicReloadableFlag(quic_disable_version_draft_27) ||
      disable_version_t051_ !=
          GetQuicReloadableFlag(quic_disable_version_t051) ||
      disable_version_t050_ !=
          GetQuicReloadableFlag(quic_disable_version_t050) ||
      disable_version_q050_ !=
          GetQuicReloadableFlag(quic_disable_version_q050) ||
      disable_version_q046_ !=
          GetQuicReloadableFlag(quic_disable_version_q046) ||
      disable_version_q043_ !=
          GetQuicReloadableFlag(quic_disable_version_q043)) {
    disable_version_draft_29_ =
        GetQuicReloadableFlag(quic_disable_version_draft_29);
    disable_version_draft_27_ =
        GetQuicReloadableFlag(quic_disable_version_draft_27);
    disable_version_t051_ = GetQuicReloadableFlag(quic_disable_version_t051);
    disable_version_t050_ = GetQuicReloadableFlag(quic_disable_version_t050);
    disable_version_q050_ = GetQuicReloadableFlag(quic_disable_version_q050);
    disable_version_q046_ = GetQuicReloadableFlag(quic_disable_version_q046);
    disable_version_q043_ = GetQuicReloadableFlag(quic_disable_version_q043);

    RefilterSupportedVersions();
  }
}

void QuicVersionManager::RefilterSupportedVersions() {
  filtered_supported_versions_ =
      FilterSupportedVersions(allowed_supported_versions_);
  filtered_supported_versions_with_quic_crypto_.clear();
  filtered_transport_versions_.clear();
  filtered_supported_alpns_.clear();
  for (const ParsedQuicVersion& version : filtered_supported_versions_) {
    auto transport_version = version.transport_version;
    if (std::find(filtered_transport_versions_.begin(),
                  filtered_transport_versions_.end(),
                  transport_version) == filtered_transport_versions_.end()) {
      filtered_transport_versions_.push_back(transport_version);
    }
    if (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
      filtered_supported_versions_with_quic_crypto_.push_back(version);
    }
    filtered_supported_alpns_.emplace_back(AlpnForVersion(version));
  }
}

void QuicVersionManager::AddCustomAlpn(const std::string& alpn) {
  filtered_supported_alpns_.push_back(alpn);
}

}  // namespace quic
