// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CRYPTO_SECRET_BOXER_H_
#define QUICHE_QUIC_CORE_CRYPTO_CRYPTO_SECRET_BOXER_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mutex.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuicRandom;

// CryptoSecretBoxer encrypts small chunks of plaintext (called 'boxing') and
// then, later, can authenticate+decrypt the resulting boxes. This object is
// thread-safe.
class QUIC_EXPORT_PRIVATE CryptoSecretBoxer {
 public:
  CryptoSecretBoxer();
  CryptoSecretBoxer(const CryptoSecretBoxer&) = delete;
  CryptoSecretBoxer& operator=(const CryptoSecretBoxer&) = delete;
  ~CryptoSecretBoxer();

  // GetKeySize returns the number of bytes in a key.
  static size_t GetKeySize();

  // SetKeys sets a list of encryption keys. The first key in the list will be
  // used by |Box|, but all supplied keys will be tried by |Unbox|, to handle
  // key skew across the fleet. This must be called before |Box| or |Unbox|.
  // Keys must be |GetKeySize()| bytes long.
  void SetKeys(const std::vector<std::string>& keys);

  // Box encrypts |plaintext| using a random nonce generated from |rand| and
  // returns the resulting ciphertext. Since an authenticator and nonce are
  // included, the result will be slightly larger than |plaintext|. The first
  // key in the vector supplied to |SetKeys| will be used.
  std::string Box(QuicRandom* rand, quiche::QuicheStringPiece plaintext) const;

  // Unbox takes the result of a previous call to |Box| in |ciphertext| and
  // authenticates+decrypts it. If |ciphertext| cannot be decrypted with any of
  // the supplied keys, the function returns false. Otherwise, |out_storage| is
  // used to store the result and |out| is set to point into |out_storage| and
  // contains the original plaintext.
  bool Unbox(quiche::QuicheStringPiece ciphertext,
             std::string* out_storage,
             quiche::QuicheStringPiece* out) const;

 private:
  struct State;

  mutable QuicMutex lock_;

  // state_ is an opaque pointer to whatever additional state the concrete
  // implementation of CryptoSecretBoxer requires.
  std::unique_ptr<State> state_ QUIC_GUARDED_BY(lock_);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CRYPTO_SECRET_BOXER_H_
