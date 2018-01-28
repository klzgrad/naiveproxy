// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/known_roots_win.h"

#include "base/metrics/histogram_macros.h"
#include "crypto/sha2.h"
#include "net/base/hash_value.h"
#include "net/cert/x509_certificate_known_roots_win.h"
#include "net/cert/x509_util_win.h"

namespace net {

bool IsKnownRoot(PCCERT_CONTEXT cert) {
  SHA256HashValue hash = x509_util::CalculateFingerprint256(cert);
  bool is_builtin =
      IsSHA256HashInSortedArray(HashValue(hash), kKnownRootCertSHA256Hashes,
                                kKnownRootCertSHA256HashesLength);

  // Test to see if the use of a built-in set of known roots on Windows can be
  // replaced with using AuthRoot's SHA-256 property. On any system other than
  // a fresh RTM with no AuthRoot updates, this property should always exist for
  // roots delivered via AuthRoot.stl, but should not exist on any manually or
  // administratively deployed roots.
  BYTE hash_prop[32] = {0};
  DWORD size = sizeof(hash_prop);
  bool found_property =
      CertGetCertificateContextProperty(
          cert, CERT_AUTH_ROOT_SHA256_HASH_PROP_ID, &hash_prop, &size) &&
      size == sizeof(hash_prop);

  enum BuiltinStatus {
    BUILT_IN_PROPERTY_NOT_FOUND_BUILTIN_NOT_SET = 0,
    BUILT_IN_PROPERTY_NOT_FOUND_BUILTIN_SET = 1,
    BUILT_IN_PROPERTY_FOUND_BUILTIN_NOT_SET = 2,
    BUILT_IN_PROPERTY_FOUND_BUILTIN_SET = 3,
    BUILT_IN_MAX_VALUE,
  } status;
  if (!found_property && !is_builtin) {
    status = BUILT_IN_PROPERTY_NOT_FOUND_BUILTIN_NOT_SET;
  } else if (!found_property && is_builtin) {
    status = BUILT_IN_PROPERTY_NOT_FOUND_BUILTIN_SET;
  } else if (found_property && !is_builtin) {
    status = BUILT_IN_PROPERTY_FOUND_BUILTIN_NOT_SET;
  } else if (found_property && is_builtin) {
    status = BUILT_IN_PROPERTY_FOUND_BUILTIN_SET;
  } else {
    status = BUILT_IN_MAX_VALUE;
  }
  UMA_HISTOGRAM_ENUMERATION("Net.SSL_AuthRootConsistency", status,
                            BUILT_IN_MAX_VALUE);

  return is_builtin;
}

}  // namespace net
