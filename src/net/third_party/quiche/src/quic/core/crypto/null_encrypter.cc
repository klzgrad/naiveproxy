// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"

#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

const size_t kHashSizeShort = 12;  // size of uint128 serialized short

NullEncrypter::NullEncrypter(Perspective perspective)
    : perspective_(perspective) {}

bool NullEncrypter::SetKey(quiche::QuicheStringPiece key) {
  return key.empty();
}

bool NullEncrypter::SetNoncePrefix(quiche::QuicheStringPiece nonce_prefix) {
  return nonce_prefix.empty();
}

bool NullEncrypter::SetIV(quiche::QuicheStringPiece iv) {
  return iv.empty();
}

bool NullEncrypter::SetHeaderProtectionKey(quiche::QuicheStringPiece key) {
  return key.empty();
}

bool NullEncrypter::EncryptPacket(uint64_t /*packet_number*/,
                                  quiche::QuicheStringPiece associated_data,
                                  quiche::QuicheStringPiece plaintext,
                                  char* output,
                                  size_t* output_length,
                                  size_t max_output_length) {
  const size_t len = plaintext.size() + GetHashLength();
  if (max_output_length < len) {
    return false;
  }
  QuicUint128 hash;
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
    quiche::QuicheStringPiece /*sample*/) {
  return std::string(5, 0);
}

size_t NullEncrypter::GetKeySize() const {
  return 0;
}

size_t NullEncrypter::GetNoncePrefixSize() const {
  return 0;
}

size_t NullEncrypter::GetIVSize() const {
  return 0;
}

size_t NullEncrypter::GetMaxPlaintextSize(size_t ciphertext_size) const {
  return ciphertext_size - std::min(ciphertext_size, GetHashLength());
}

size_t NullEncrypter::GetCiphertextSize(size_t plaintext_size) const {
  return plaintext_size + GetHashLength();
}

quiche::QuicheStringPiece NullEncrypter::GetKey() const {
  return quiche::QuicheStringPiece();
}

quiche::QuicheStringPiece NullEncrypter::GetNoncePrefix() const {
  return quiche::QuicheStringPiece();
}

size_t NullEncrypter::GetHashLength() const {
  return kHashSizeShort;
}

}  // namespace quic
