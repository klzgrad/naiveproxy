// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_PRIVATE_KEY_H_
#define NET_SSL_SSL_PRIVATE_KEY_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_piece.h"
#include "net/base/net_errors.h"

namespace net {

// An interface for a private key for use with SSL client authentication.
class SSLPrivateKey : public base::RefCountedThreadSafe<SSLPrivateKey> {
 public:
  using SignCallback = base::Callback<void(Error, const std::vector<uint8_t>&)>;

  enum class Hash {
    MD5_SHA1,
    SHA1,
    SHA256,
    SHA384,
    SHA512,
  };

  SSLPrivateKey() {}

  // Returns the digests that are supported by the key in decreasing preference.
  virtual std::vector<SSLPrivateKey::Hash> GetDigestPreferences() = 0;

  // Asynchronously signs an |input| which was computed with the hash |hash|. On
  // completion, it calls |callback| with the signature or an error code if the
  // operation failed. For an RSA key, the signature is a PKCS#1 signature. The
  // SSLPrivateKey implementation is responsible for prepending the DigestInfo
  // prefix and adding PKCS#1 padding.
  virtual void SignDigest(Hash hash,
                          const base::StringPiece& input,
                          const SignCallback& callback) = 0;

 protected:
  virtual ~SSLPrivateKey() {}

 private:
  friend class base::RefCountedThreadSafe<SSLPrivateKey>;
  DISALLOW_COPY_AND_ASSIGN(SSLPrivateKey);
};

}  // namespace net

#endif  // NET_SSL_SSL_PRIVATE_KEY_H_
