// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_AEAD_BASE_DECRYPTER_H_
#define NET_QUIC_CORE_CRYPTO_AEAD_BASE_DECRYPTER_H_

#include <cstddef>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/quic/core/crypto/quic_decrypter.h"
#include "net/quic/core/crypto/scoped_evp_aead_ctx.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

// AeadBaseDecrypter is the base class of AEAD QuicDecrypter subclasses.
class QUIC_EXPORT_PRIVATE AeadBaseDecrypter : public QuicDecrypter {
 public:
  AeadBaseDecrypter(const EVP_AEAD* aead_alg,
                    size_t key_size,
                    size_t auth_tag_size,
                    size_t nonce_size,
                    bool use_ietf_nonce_construction);
  ~AeadBaseDecrypter() override;

  // QuicDecrypter implementation
  bool SetKey(QuicStringPiece key) override;
  bool SetNoncePrefix(QuicStringPiece nonce_prefix) override;
  bool SetIV(QuicStringPiece iv) override;
  bool SetPreliminaryKey(QuicStringPiece key) override;
  bool SetDiversificationNonce(const DiversificationNonce& nonce) override;
  bool DecryptPacket(QuicTransportVersion version,
                     QuicPacketNumber packet_number,
                     QuicStringPiece associated_data,
                     QuicStringPiece ciphertext,
                     char* output,
                     size_t* output_length,
                     size_t max_output_length) override;
  QuicStringPiece GetKey() const override;
  QuicStringPiece GetNoncePrefix() const override;

 protected:
  // Make these constants available to the subclasses so that the subclasses
  // can assert at compile time their key_size_ and nonce_size_ do not
  // exceed the maximum.
  static const size_t kMaxKeySize = 32;
  static const size_t kMaxNonceSize = 12;

 private:
  const EVP_AEAD* const aead_alg_;
  const size_t key_size_;
  const size_t auth_tag_size_;
  const size_t nonce_size_;
  const bool use_ietf_nonce_construction_;
  bool have_preliminary_key_;

  // The key.
  unsigned char key_[kMaxKeySize];
  // The IV used to construct the nonce.
  unsigned char iv_[kMaxNonceSize];

  ScopedEVPAEADCtx ctx_;

  DISALLOW_COPY_AND_ASSIGN(AeadBaseDecrypter);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_AEAD_BASE_DECRYPTER_H_
