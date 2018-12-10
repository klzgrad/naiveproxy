// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_CRYPTO_SCOPED_EVP_AEAD_CTX_H_
#define NET_THIRD_PARTY_QUIC_CORE_CRYPTO_SCOPED_EVP_AEAD_CTX_H_

#include "base/macros.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

namespace quic {

// ScopedEVPAEADCtx manages an EVP_AEAD_CTX object and calls the needed cleanup
// functions when it goes out of scope.
class ScopedEVPAEADCtx {
 public:
  ScopedEVPAEADCtx();
  ScopedEVPAEADCtx(const ScopedEVPAEADCtx&) = delete;
  ScopedEVPAEADCtx& operator=(const ScopedEVPAEADCtx&) = delete;
  ~ScopedEVPAEADCtx();

  EVP_AEAD_CTX* get();

 private:
  EVP_AEAD_CTX ctx_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_CRYPTO_SCOPED_EVP_AEAD_CTX_H_
