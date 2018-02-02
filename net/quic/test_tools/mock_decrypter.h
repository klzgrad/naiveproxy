// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_MOCK_DECRYPTER_H_
#define NET_QUIC_TEST_TOOLS_MOCK_DECRYPTER_H_

#include <cstddef>
#include <cstdint>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/quic/core/crypto/quic_decrypter.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

// A MockDecrypter is a QuicDecrypter that does no validation of
// the given ciphertext and returns it untouched, ignoring the
// associated data. This is used to allow fuzzing to mutate
// plaintext packets.
class MockDecrypter : public QuicDecrypter {
 public:
  explicit MockDecrypter(Perspective perspective);
  ~MockDecrypter() override {}

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
  DISALLOW_COPY_AND_ASSIGN(MockDecrypter);
};

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_MOCK_DECRYPTER_H_
