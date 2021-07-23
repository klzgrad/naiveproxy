// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_VERSION_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_VERSION_MANAGER_H_

#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_export.h"

namespace quic {

// Used to generate filtered supported versions based on flags.
class QUIC_EXPORT_PRIVATE QuicVersionManager {
 public:
  // |supported_versions| should be sorted in the order of preference (typically
  // highest supported version to the lowest supported version).
  explicit QuicVersionManager(ParsedQuicVersionVector supported_versions);
  virtual ~QuicVersionManager();

  // Returns currently supported QUIC versions. This vector has the same order
  // as the versions passed to the constructor.
  const ParsedQuicVersionVector& GetSupportedVersions();

  // Returns currently supported versions using QUIC crypto.
  const ParsedQuicVersionVector& GetSupportedVersionsWithQuicCrypto();

  // Returns the list of supported ALPNs, based on the current supported
  // versions and any custom additions by subclasses.
  const std::vector<std::string>& GetSupportedAlpns();

 protected:
  // If the value of any reloadable flag is different from the cached value,
  // re-filter |filtered_supported_versions_| and update the cached flag values.
  // Otherwise, does nothing.
  void MaybeRefilterSupportedVersions();

  // Refilters filtered_supported_versions_.
  virtual void RefilterSupportedVersions();

  const QuicTransportVersionVector& filtered_transport_versions() const {
    return filtered_transport_versions_;
  }

  // Mechanism for subclasses to add custom ALPNs to the supported list.
  // Should be called in constructor and RefilterSupportedVersions.
  void AddCustomAlpn(const std::string& alpn);

  bool disable_version_q050() const { return disable_version_q050_; }

 private:
  // Cached value of reloadable flags.
  // quic_enable_version_rfcv1 flag
  bool enable_version_rfcv1_;
  // quic_disable_version_draft_29 flag
  bool disable_version_draft_29_;
  // quic_disable_version_t051 flag
  bool disable_version_t051_;
  // quic_disable_version_q050 flag
  bool disable_version_q050_;
  // quic_disable_version_q046 flag
  bool disable_version_q046_;
  // quic_disable_version_q043 flag
  bool disable_version_q043_;

  // The list of versions that may be supported.
  ParsedQuicVersionVector allowed_supported_versions_;
  // This vector contains QUIC versions which are currently supported based on
  // flags.
  ParsedQuicVersionVector filtered_supported_versions_;
  // Currently supported versions using QUIC crypto.
  ParsedQuicVersionVector filtered_supported_versions_with_quic_crypto_;
  // This vector contains the transport versions from
  // |filtered_supported_versions_|. No guarantees are made that the same
  // transport version isn't repeated.
  QuicTransportVersionVector filtered_transport_versions_;
  // Contains the list of ALPNs corresponding to filtered_supported_versions_
  // with custom ALPNs added.
  std::vector<std::string> filtered_supported_alpns_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_VERSION_MANAGER_H_
