// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/null_decrypter.h"

#include <cstdint>

#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/quiche_endian.h"

namespace quic {

NullDecrypter::NullDecrypter(Perspective perspective)
    : perspective_(perspective) {}

bool NullDecrypter::SetKey(absl::string_view key) { return key.empty(); }

bool NullDecrypter::SetNoncePrefix(absl::string_view nonce_prefix) {
  return nonce_prefix.empty();
}

bool NullDecrypter::SetIV(absl::string_view iv) { return iv.empty(); }

bool NullDecrypter::SetHeaderProtectionKey(absl::string_view key) {
  return key.empty();
}

bool NullDecrypter::SetPreliminaryKey(absl::string_view /*key*/) {
  QUIC_BUG(quic_bug_10652_1) << "Should not be called";
  return false;
}

bool NullDecrypter::SetDiversificationNonce(
    const DiversificationNonce& /*nonce*/) {
  QUIC_BUG(quic_bug_10652_2) << "Should not be called";
  return true;
}

bool NullDecrypter::DecryptPacket(uint64_t /*packet_number*/,
                                  absl::string_view associated_data,
                                  absl::string_view ciphertext, char* output,
                                  size_t* output_length,
                                  size_t max_output_length) {
  QuicDataReader reader(ciphertext.data(), ciphertext.length(),
                        quiche::HOST_BYTE_ORDER);
  absl::uint128 hash;

  if (!ReadHash(&reader, &hash)) {
    return false;
  }

  absl::string_view plaintext = reader.ReadRemainingPayload();
  if (plaintext.length() > max_output_length) {
    QUIC_BUG(quic_bug_10652_3)
        << "Output buffer must be larger than the plaintext.";
    return false;
  }
  if (hash != ComputeHash(associated_data, plaintext)) {
    return false;
  }
  // Copy the plaintext to output.
  memcpy(output, plaintext.data(), plaintext.length());
  *output_length = plaintext.length();
  return true;
}

std::string NullDecrypter::GenerateHeaderProtectionMask(
    QuicDataReader* /*sample_reader*/) {
  return std::string(5, 0);
}

size_t NullDecrypter::GetKeySize() const { return 0; }

size_t NullDecrypter::GetNoncePrefixSize() const { return 0; }

size_t NullDecrypter::GetIVSize() const { return 0; }

absl::string_view NullDecrypter::GetKey() const { return absl::string_view(); }

absl::string_view NullDecrypter::GetNoncePrefix() const {
  return absl::string_view();
}

uint32_t NullDecrypter::cipher_id() const { return 0; }

QuicPacketCount NullDecrypter::GetIntegrityLimit() const {
  return std::numeric_limits<QuicPacketCount>::max();
}

bool NullDecrypter::ReadHash(QuicDataReader* reader, absl::uint128* hash) {
  uint64_t lo;
  uint32_t hi;
  if (!reader->ReadUInt64(&lo) || !reader->ReadUInt32(&hi)) {
    return false;
  }
  *hash = absl::MakeUint128(hi, lo);
  return true;
}

absl::uint128 NullDecrypter::ComputeHash(const absl::string_view data1,
                                         const absl::string_view data2) const {
  absl::uint128 correct_hash;
  if (perspective_ == Perspective::IS_CLIENT) {
    // Peer is a server.
    correct_hash = QuicUtils::FNV1a_128_Hash_Three(data1, data2, "Server");
  } else {
    // Peer is a client.
    correct_hash = QuicUtils::FNV1a_128_Hash_Three(data1, data2, "Client");
  }
  absl::uint128 mask = absl::MakeUint128(UINT64_C(0x0), UINT64_C(0xffffffff));
  mask <<= 96;
  correct_hash &= ~mask;
  return correct_hash;
}

}  // namespace quic
