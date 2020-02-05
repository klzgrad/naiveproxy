// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

#include <algorithm>

namespace quic {

QuicVersionManager::QuicVersionManager(
    ParsedQuicVersionVector supported_versions)
    : enable_version_99_(GetQuicReloadableFlag(quic_enable_version_99)),
      enable_version_50_(GetQuicReloadableFlag(quic_enable_version_50)),
      enable_tls_(GetQuicReloadableFlag(quic_supports_tls_handshake)),
      allowed_supported_versions_(std::move(supported_versions)) {
  static_assert(QUIC_ARRAYSIZE(kSupportedTransportVersions) == 6u,
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
  static_assert(QUIC_ARRAYSIZE(kSupportedTransportVersions) == 6u,
                "Supported versions out of sync");
  if (enable_version_99_ != GetQuicReloadableFlag(quic_enable_version_99) ||
      enable_version_50_ != GetQuicReloadableFlag(quic_enable_version_50) ||
      enable_tls_ != GetQuicReloadableFlag(quic_supports_tls_handshake)) {
    enable_version_99_ = GetQuicReloadableFlag(quic_enable_version_99);
    enable_version_50_ = GetQuicReloadableFlag(quic_enable_version_50);
    enable_tls_ = GetQuicReloadableFlag(quic_supports_tls_handshake);
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
