// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/null_encrypter.h"

#include <algorithm>
#include <limits>
#include <string>

#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_utils.h"

namespace quic {

const size_t kHashSizeShort = 12;  // size of uint128 serialized short

NullEncrypter::NullEncrypter(Perspective perspective)
    : perspective_(perspective) {}

bool NullEncrypter::SetKey(absl::string_view key) { return key.empty(); }

bool NullEncrypter::SetNoncePrefix(absl::string_view nonce_prefix) {
  return nonce_prefix.empty();
}

bool NullEncrypter::SetIV(absl::string_view iv) { return iv.empty(); }

bool NullEncrypter::SetHeaderProtectionKey(absl::string_view key) {
  return key.empty();
}

bool NullEncrypter::EncryptPacket(uint64_t /*packet_number*/,
                                  absl::string_view associated_data,
                                  absl::string_view plaintext, char* output,
                                  size_t* output_length,
                                  size_t max_output_length) {
  const size_t len = plaintext.size() + GetHashLength();
  if (max_output_length < len) {
    return false;
  }
  absl::uint128 hash;
  if (perspective_ == Perspective::IS_SERVER) {
    hash =
        QuicUtils::FNV1a_128_Hash_Three(associated_data, plaintext, "Server");
  } else {
    hash =
        QuicUtils::FNV1a_128_Hash_Three(associated_data, plaintext, "Client");
  }
  // TODO(ianswett): memmove required for in place encryption.  Placing the
  // hash at the end would allow use of memcpy, doing nothing for in place.
  memmove(output + GetHashLength(), plaintext.data(), plaintext.length());
  QuicUtils::SerializeUint128Short(hash,
                                   reinterpret_cast<unsigned char*>(output));
  *output_length = len;
  return true;
}

std::string NullEncrypter::GenerateHeaderProtectionMask(
    absl::string_view /*sample*/) {
  return std::string(5, 0);
}

size_t NullEncrypter::GetKeySize() const { return 0; }

size_t NullEncrypter::GetNoncePrefixSize() const { return 0; }

size_t NullEncrypter::GetIVSize() const { return 0; }

size_t NullEncrypter::GetMaxPlaintextSize(size_t ciphertext_size) const {
  return ciphertext_size - std::min(ciphertext_size, GetHashLength());
}

size_t NullEncrypter::GetCiphertextSize(size_t plaintext_size) const {
  return plaintext_size + GetHashLength();
}

QuicPacketCount NullEncrypter::GetConfidentialityLimit() const {
  return std::numeric_limits<QuicPacketCount>::max();
}

absl::string_view NullEncrypter::GetKey() const { return absl::string_view(); }

absl::string_view NullEncrypter::GetNoncePrefix() const {
  return absl::string_view();
}

size_t NullEncrypter::GetHashLength() const { return kHashSizeShort; }

}  // namespace quic
