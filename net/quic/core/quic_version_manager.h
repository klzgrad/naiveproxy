// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_VERSION_MANAGER_H_
#define NET_QUIC_CORE_QUIC_VERSION_MANAGER_H_

#include "net/quic/core/quic_versions.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// Used to generate filtered supported versions based on flags.
class QUIC_EXPORT_PRIVATE QuicVersionManager {
 public:
  explicit QuicVersionManager(QuicTransportVersionVector supported_versions);
  virtual ~QuicVersionManager();

  // Returns currently supported QUIC versions.
  const QuicTransportVersionVector& GetSupportedTransportVersions();

 protected:
  // Maybe refilter filtered_supported_versions_ based on flags.
  void MaybeRefilterSupportedTransportVersions();

  // Refilters filtered_supported_versions_.
  virtual void RefilterSupportedTransportVersions();

  const QuicTransportVersionVector& filtered_supported_versions() const {
    return filtered_supported_versions_;
  }

 private:
  // FLAGS_quic_enable_version_43
  bool enable_version_43_;
  // FLAGS_quic_enable_version_42
  bool enable_version_42_;
  // FLAGS_quic_reloadable_flag_quic_enable_version_41
  bool enable_version_41_;
  // FLAGS_quic_reloadable_flag_quic_enable_version_39
  bool enable_version_39_;
  // FLAGS_quic_reloadable_flag_quic_enable_version_38
  bool enable_version_38_;
  // The list of versions that may be supported.
  QuicTransportVersionVector allowed_supported_versions_;
  // This vector contains QUIC versions which are currently supported based on
  // flags.
  QuicTransportVersionVector filtered_supported_versions_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_VERSION_MANAGER_H_
