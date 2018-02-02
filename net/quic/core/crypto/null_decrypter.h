// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_
#define NET_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_

#include <cstddef>
#include <cstdint>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/base/int128.h"
#include "net/quic/core/crypto/quic_decrypter.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

class QuicDataReader;

// A NullDecrypter is a QuicDecrypter used before a crypto negotiation
// has occurred.  It does not actually decrypt the payload, but does
// verify a hash (fnv128) over both the payload and associated data.
class QUIC_EXPORT_PRIVATE NullDecrypter : public QuicDecrypter {
 public:
  explicit NullDecrypter(Perspective perspective);
  ~NullDecrypter() override {}

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

  uint32_t cipher_id() const override;

 private:
  bool ReadHash(QuicDataReader* reader, uint128* hash);
  uint128 ComputeHash(QuicTransportVersion version,
                      QuicStringPiece data1,
                      QuicStringPiece data2) const;

  Perspective perspective_;

  DISALLOW_COPY_AND_ASSIGN(NullDecrypter);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_
