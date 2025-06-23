// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_VERSION_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_VERSION_MANAGER_H_

#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Used to generate filtered supported versions based on flags.
class QUICHE_EXPORT QuicVersionManager {
 public:
  // |supported_versions| should be sorted in the order of preference (typically
  // highest supported version to the lowest supported version).
  explicit QuicVersionManager(ParsedQuicVersionVector supported_versions);
  virtual ~QuicVersionManager();

  // Returns currently supported QUIC versions. This vector has the same order
  // as the versions passed to the constructor.
  const ParsedQuicVersionVector& GetSupportedVersions();

  // Returns currently supported versions using HTTP/3.
  const ParsedQuicVersionVector& GetSupportedVersionsWithOnlyHttp3();

  // Returns the list of supported ALPNs, based on the current supported
  // versions and any custom additions by subclasses.
  const std::vector<std::string>& GetSupportedAlpns();

 protected:
  // If the value of any reloadable flag is different from the cached value,
  // re-filter |filtered_supported_versions_| and update the cached flag values.
  // Otherwise, does nothing.
  // TODO(dschinazi): Make private when deprecating
  // FLAGS_gfe2_restart_flag_quic_disable_old_alt_svc_format.
  void MaybeRefilterSupportedVersions();

  // Refilters filtered_supported_versions_.
  virtual void RefilterSupportedVersions();

  // RefilterSupportedVersions() must be called before calling this method.
  // TODO(dschinazi): Remove when deprecating
  // FLAGS_gfe2_restart_flag_quic_disable_old_alt_svc_format.
  const QuicTransportVersionVector& filtered_transport_versions() const {
    return filtered_transport_versions_;
  }

  // Subclasses may add custom ALPNs to the supported list by overriding
  // RefilterSupportedVersions() to first call
  // QuicVersionManager::RefilterSupportedVersions() then AddCustomAlpn().
  // Must not be called elsewhere.
  void AddCustomAlpn(const std::string& alpn);

 private:
  // Cached value of reloadable flags.
  // quic_enable_version_2_draft_08 flag
  bool enable_version_2_draft_08_ = false;
  // quic_disable_version_rfcv1 flag
  bool disable_version_rfcv1_ = true;
  // quic_disable_version_draft_29 flag
  bool disable_version_draft_29_ = true;
  // quic_disable_version_q046 flag
  bool disable_version_q046_ = true;

  // The list of versions that may be supported.
  const ParsedQuicVersionVector allowed_supported_versions_;

  // The following vectors are calculated from reloadable flags by
  // RefilterSupportedVersions().  It is performed lazily when first needed, and
  // after that, since the calculation is relatively expensive, only if the flag
  // values change.

  // This vector contains QUIC versions which are currently supported based on
  // flags.
  ParsedQuicVersionVector filtered_supported_versions_;
  // Currently supported versions using HTTP/3.
  ParsedQuicVersionVector filtered_supported_versions_with_http3_;
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
