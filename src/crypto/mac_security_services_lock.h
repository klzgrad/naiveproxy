// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_MAC_SECURITY_SERVICES_LOCK_H_
#define CRYPTO_MAC_SECURITY_SERVICES_LOCK_H_

#include "crypto/crypto_export.h"

namespace base {
class Lock;
}

namespace crypto {

// The Mac OS X certificate and key management wrappers over CSSM are not
// thread-safe. In particular, code that accesses the CSSM database is
// problematic.
//
// https://developer.apple.com/documentation/security/certificate_key_and_trust_services/working_with_concurrency
CRYPTO_EXPORT base::Lock& GetMacSecurityServicesLock();

}  // namespace crypto

#endif  // CRYPTO_MAC_SECURITY_SERVICES_LOCK_H_
