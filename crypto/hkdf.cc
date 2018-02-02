// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/hkdf.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/logging.h"
#include "crypto/hmac.h"

namespace crypto {

const size_t kSHA256HashLength = 32;

HKDF::HKDF(base::StringPiece secret,
           base::StringPiece salt,
           base::StringPiece info,
           size_t key_bytes_to_generate,
           size_t iv_bytes_to_generate,
           size_t subkey_secret_bytes_to_generate)
    : HKDF(secret,
           salt,
           info,
           key_bytes_to_generate,
           key_bytes_to_generate,
           iv_bytes_to_generate,
           iv_bytes_to_generate,
           subkey_secret_bytes_to_generate) {}

HKDF::HKDF(base::StringPiece secret,
           base::StringPiece salt,
           base::StringPiece info,
           size_t client_key_bytes_to_generate,
           size_t server_key_bytes_to_generate,
           size_t client_iv_bytes_to_generate,
           size_t server_iv_bytes_to_generate,
           size_t subkey_secret_bytes_to_generate) {
  // https://tools.ietf.org/html/rfc5869#section-2.2
  base::StringPiece actual_salt = salt;
  char zeros[kSHA256HashLength];
  if (actual_salt.empty()) {
    // If salt is not given, HashLength zeros are used.
    memset(zeros, 0, sizeof(zeros));
    actual_salt.set(zeros, sizeof(zeros));
  }

  // Perform the Extract step to transform the input key and
  // salt into the pseudorandom key (PRK) used for Expand.
  HMAC prk_hmac(HMAC::SHA256);
  bool result = prk_hmac.Init(actual_salt);
  DCHECK(result);

  // |prk| is a pseudorandom key (of kSHA256HashLength octets).
  uint8_t prk[kSHA256HashLength];
  DCHECK_EQ(sizeof(prk), prk_hmac.DigestLength());
  result = prk_hmac.Sign(secret, prk, sizeof(prk));
  DCHECK(result);

  // https://tools.ietf.org/html/rfc5869#section-2.3
  // Perform the Expand phase to turn the pseudorandom key
  // and info into the output keying material.
  const size_t material_length =
      client_key_bytes_to_generate + client_iv_bytes_to_generate +
      server_key_bytes_to_generate + server_iv_bytes_to_generate +
      subkey_secret_bytes_to_generate;
  const size_t n =
      (material_length + kSHA256HashLength - 1) / kSHA256HashLength;
  DCHECK_LT(n, 256u);

  output_.resize(n * kSHA256HashLength);
  base::StringPiece previous;

  std::unique_ptr<char[]> buf(new char[kSHA256HashLength + info.size() + 1]);
  uint8_t digest[kSHA256HashLength];

  HMAC hmac(HMAC::SHA256);
  result = hmac.Init(prk, sizeof(prk));
  DCHECK(result);

  for (size_t i = 0; i < n; i++) {
    memcpy(buf.get(), previous.data(), previous.size());
    size_t j = previous.size();
    memcpy(buf.get() + j, info.data(), info.size());
    j += info.size();
    buf[j++] = static_cast<char>(i + 1);

    result = hmac.Sign(base::StringPiece(buf.get(), j), digest, sizeof(digest));
    DCHECK(result);

    memcpy(&output_[i*sizeof(digest)], digest, sizeof(digest));
    previous = base::StringPiece(reinterpret_cast<char*>(digest),
                                 sizeof(digest));
  }

  size_t j = 0;
  // On Windows, when the size of output_ is zero, dereference of 0'th element
  // results in a crash. C++11 solves this problem by adding a data() getter
  // method to std::vector.
  if (client_key_bytes_to_generate) {
    client_write_key_ = base::StringPiece(reinterpret_cast<char*>(&output_[j]),
                                          client_key_bytes_to_generate);
    j += client_key_bytes_to_generate;
  }

  if (server_key_bytes_to_generate) {
    server_write_key_ = base::StringPiece(reinterpret_cast<char*>(&output_[j]),
                                          server_key_bytes_to_generate);
    j += server_key_bytes_to_generate;
  }

  if (client_iv_bytes_to_generate) {
    client_write_iv_ = base::StringPiece(reinterpret_cast<char*>(&output_[j]),
                                         client_iv_bytes_to_generate);
    j += client_iv_bytes_to_generate;
  }

  if (server_iv_bytes_to_generate) {
    server_write_iv_ = base::StringPiece(reinterpret_cast<char*>(&output_[j]),
                                         server_iv_bytes_to_generate);
    j += server_iv_bytes_to_generate;
  }

  if (subkey_secret_bytes_to_generate) {
    subkey_secret_ = base::StringPiece(reinterpret_cast<char*>(&output_[j]),
                                       subkey_secret_bytes_to_generate);
  }
}

HKDF::~HKDF() = default;

}  // namespace crypto
