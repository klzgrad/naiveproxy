// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

#include <algorithm>

namespace quic {

QuicVersionManager::QuicVersionManager(
    ParsedQuicVersionVector supported_versions)
    : enable_version_99_(GetQuicReloadableFlag(quic_enable_version_99)),
      enable_version_48_(GetQuicReloadableFlag(quic_enable_version_48_2)),
      enable_version_47_(GetQuicReloadableFlag(quic_enable_version_47)),
      disable_version_39_(GetQuicReloadableFlag(quic_disable_version_39)),
      enable_tls_(GetQuicFlag(FLAGS_quic_supports_tls_handshake)),
      allowed_supported_versions_(std::move(supported_versions)) {
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
  if (enable_version_99_ != GetQuicReloadableFlag(quic_enable_version_99) ||
      enable_version_48_ != GetQuicReloadableFlag(quic_enable_version_48_2) ||
      enable_version_47_ != GetQuicReloadableFlag(quic_enable_version_47) ||
      disable_version_39_ != GetQuicReloadableFlag(quic_disable_version_39) ||
      enable_tls_ != GetQuicFlag(FLAGS_quic_supports_tls_handshake)) {
    enable_version_99_ = GetQuicReloadableFlag(quic_enable_version_99);
    enable_version_48_ = GetQuicReloadableFlag(quic_enable_version_48_2);
    enable_version_47_ = GetQuicReloadableFlag(quic_enable_version_47);
    disable_version_39_ = GetQuicReloadableFlag(quic_disable_version_39);
    enable_tls_ = GetQuicFlag(FLAGS_quic_supports_tls_handshake);
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
