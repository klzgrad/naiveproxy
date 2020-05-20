// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_VERSION_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_VERSION_MANAGER_H_

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// Used to generate filtered supported versions based on flags.
class QUIC_EXPORT_PRIVATE QuicVersionManager {
 public:
  // |supported_versions| should be sorted in the order of preference (typically
  // highest supported version to the lowest supported version).
  explicit QuicVersionManager(ParsedQuicVersionVector supported_versions);
  virtual ~QuicVersionManager();

  // Returns currently supported QUIC versions.
  // TODO(nharper): Remove this method once it is unused.
  const QuicTransportVersionVector& GetSupportedTransportVersions();

  // Returns currently supported QUIC versions. This vector has the same order
  // as the versions passed to the constructor.
  const ParsedQuicVersionVector& GetSupportedVersions();

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

 private:
  // Cached value of reloadable flags.
  // quic_enable_version_draft_27 flag
  bool enable_version_draft_27_;
  // quic_enable_version_draft_25_v3 flag
  bool enable_version_draft_25_;
  // quic_disable_version_q050 flag
  bool disable_version_q050_;
  // quic_enable_version_t050 flag
  bool enable_version_t050_;
  // quic_disable_version_q049 flag
  bool disable_version_q049_;
  // quic_disable_version_q048 flag
  bool disable_version_q048_;
  // quic_disable_version_q046 flag
  bool disable_version_q046_;
  // quic_disable_version_q043 flag
  bool disable_version_q043_;

  // The list of versions that may be supported.
  ParsedQuicVersionVector allowed_supported_versions_;
  // This vector contains QUIC versions which are currently supported based on
  // flags.
  ParsedQuicVersionVector filtered_supported_versions_;
  // This vector contains the transport versions from
  // |filtered_supported_versions_|. No guarantees are made that the same
  // transport version isn't repeated.
  QuicTransportVersionVector filtered_transport_versions_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_VERSION_MANAGER_H_
