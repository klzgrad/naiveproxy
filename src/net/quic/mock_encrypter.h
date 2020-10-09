// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_MOCK_ENCRYPTER_H_
#define NET_QUIC_MOCK_ENCRYPTER_H_

#include <cstddef>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace net {

// A MockEncrypter is a QuicEncrypter that returns this plaintext followed by 12
// bytes of zeroes. No encryption or MAC is applied. This is used to allow
// fuzzing to mutate plaintext packets.
class MockEncrypter : public quic::QuicEncrypter {
 public:
  explicit MockEncrypter(quic::Perspective perspective);
  ~MockEncrypter() override {}

  // QuicEncrypter implementation
  bool SetKey(quiche::QuicheStringPiece key) override;
  bool SetNoncePrefix(quiche::QuicheStringPiece nonce_prefix) override;
  bool SetHeaderProtectionKey(quiche::QuicheStringPiece key) override;
  bool SetIV(quiche::QuicheStringPiece iv) override;
  bool EncryptPacket(uint64_t packet_number,
                     quiche::QuicheStringPiece associated_data,
                     quiche::QuicheStringPiece plaintext,
                     char* output,
                     size_t* output_length,
                     size_t max_output_length) override;
  std::string GenerateHeaderProtectionMask(
      quiche::QuicheStringPiece sample) override;
  size_t GetKeySize() const override;
  size_t GetNoncePrefixSize() const override;
  size_t GetIVSize() const override;
  size_t GetMaxPlaintextSize(size_t ciphertext_size) const override;
  size_t GetCiphertextSize(size_t plaintext_size) const override;
  quiche::QuicheStringPiece GetKey() const override;
  quiche::QuicheStringPiece GetNoncePrefix() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MockEncrypter);
};

}  // namespace net

#endif  // NET_QUIC_MOCK_ENCRYPTER_H_
