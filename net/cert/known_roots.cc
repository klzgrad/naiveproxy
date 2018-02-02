// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/known_roots.h"

#include <string.h>

#include <algorithm>

#include "net/base/hash_value.h"
#include "net/cert/root_cert_list_generated.h"

namespace net {

namespace {

// Comparator-predicate that serves as a < function for comparing a
// RootCertData to a HashValue
struct HashValueToRootCertDataComp {
  bool operator()(const HashValue& hash, const RootCertData& root_cert) {
    DCHECK_EQ(HASH_VALUE_SHA256, hash.tag);
    return memcmp(hash.data(), root_cert.sha256_spki_hash, 32) < 0;
  }

  bool operator()(const RootCertData& root_cert, const HashValue& hash) {
    DCHECK_EQ(HASH_VALUE_SHA256, hash.tag);
    return memcmp(root_cert.sha256_spki_hash, hash.data(), 32) < 0;
  }
};

}  // namespace

int32_t GetNetTrustAnchorHistogramIdForSPKI(const HashValue& spki_hash) {
  if (spki_hash.tag != HASH_VALUE_SHA256)
    return 0;

  auto* it = std::lower_bound(std::begin(kRootCerts), std::end(kRootCerts),
                              spki_hash, HashValueToRootCertDataComp());
  return (it != std::end(kRootCerts) &&
          !HashValueToRootCertDataComp()(spki_hash, *it))
             ? it->histogram_id
             : 0;
}

}  // namespace net
