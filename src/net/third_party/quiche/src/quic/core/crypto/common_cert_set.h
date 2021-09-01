// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_COMMON_CERT_SET_H_
#define QUICHE_QUIC_CORE_CRYPTO_COMMON_CERT_SET_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "quic/core/crypto/crypto_protocol.h"
#include "quic/platform/api/quic_export.h"

namespace quic {

// CommonCertSets is an interface to an object that contains a number of common
// certificate sets and can match against them.
class QUIC_EXPORT_PRIVATE CommonCertSets {
 public:
  virtual ~CommonCertSets();

  // GetInstanceQUIC returns the standard QUIC common certificate sets.
  static const CommonCertSets* GetInstanceQUIC();

  // GetCommonHashes returns a absl::string_view containing the hashes
  // of common sets supported by this object. The 64-bit hashes are concatenated
  // in the absl::string_view.
  virtual absl::string_view GetCommonHashes() const = 0;

  // GetCert returns a specific certificate (at index |index|) in the common
  // set identified by |hash|. If no such certificate is known, an empty
  // absl::string_view is returned.
  virtual absl::string_view GetCert(uint64_t hash, uint32_t index) const = 0;

  // MatchCert tries to find |cert| in one of the common certificate sets
  // identified by |common_set_hashes|. On success it puts the hash of the
  // set in |out_hash|, the index of |cert| in the set in |out_index| and
  // returns true. Otherwise it returns false.
  virtual bool MatchCert(absl::string_view cert,
                         absl::string_view common_set_hashes,
                         uint64_t* out_hash,
                         uint32_t* out_index) const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_COMMON_CERT_SET_H_
