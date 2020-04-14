// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_

#include <cstddef>
#include <cstdint>

#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_uint128.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuicDataReader;

// A NullDecrypter is a QuicDecrypter used before a crypto negotiation
// has occurred.  It does not actually decrypt the payload, but does
// verify a hash (fnv128) over both the payload and associated data.
class QUIC_EXPORT_PRIVATE NullDecrypter : public QuicDecrypter {
 public:
  explicit NullDecrypter(Perspective perspective);
  NullDecrypter(const NullDecrypter&) = delete;
  NullDecrypter& operator=(const NullDecrypter&) = delete;
  ~NullDecrypter() override {}

  // QuicDecrypter implementation
  bool SetKey(quiche::QuicheStringPiece key) override;
  bool SetNoncePrefix(quiche::QuicheStringPiece nonce_prefix) override;
  bool SetIV(quiche::QuicheStringPiece iv) override;
  bool SetHeaderProtectionKey(quiche::QuicheStringPiece key) override;
  bool SetPreliminaryKey(quiche::QuicheStringPiece key) override;
  bool SetDiversificationNonce(const DiversificationNonce& nonce) override;
  bool DecryptPacket(uint64_t packet_number,
                     quiche::QuicheStringPiece associated_data,
                     quiche::QuicheStringPiece ciphertext,
                     char* output,
                     size_t* output_length,
                     size_t max_output_length) override;
  std::string GenerateHeaderProtectionMask(
      QuicDataReader* sample_reader) override;
  size_t GetKeySize() const override;
  size_t GetNoncePrefixSize() const override;
  size_t GetIVSize() const override;
  quiche::QuicheStringPiece GetKey() const override;
  quiche::QuicheStringPiece GetNoncePrefix() const override;

  uint32_t cipher_id() const override;

 private:
  bool ReadHash(QuicDataReader* reader, QuicUint128* hash);
  QuicUint128 ComputeHash(quiche::QuicheStringPiece data1,
                          quiche::QuicheStringPiece data2) const;

  Perspective perspective_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_
