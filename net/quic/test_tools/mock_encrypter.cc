// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/mock_encrypter.h"

#include "net/quic/core/quic_data_writer.h"
#include "net/quic/core/quic_utils.h"

namespace net {

MockEncrypter::MockEncrypter(Perspective perspective) {}

bool MockEncrypter::SetKey(QuicStringPiece key) {
  return key.empty();
}

bool MockEncrypter::SetNoncePrefix(QuicStringPiece nonce_prefix) {
  return nonce_prefix.empty();
}

bool MockEncrypter::SetIV(QuicStringPiece iv) {
  return iv.empty();
}

bool MockEncrypter::EncryptPacket(QuicTransportVersion version,
                                  QuicPacketNumber /*packet_number*/,
                                  QuicStringPiece associated_data,
                                  QuicStringPiece plaintext,
                                  char* output,
                                  size_t* output_length,
                                  size_t max_output_length) {
  if (max_output_length < plaintext.size()) {
    return false;
  }
  memcpy(output, plaintext.data(), plaintext.length());
  *output_length = plaintext.size();
  return true;
}

size_t MockEncrypter::GetKeySize() const {
  return 0;
}

size_t MockEncrypter::GetNoncePrefixSize() const {
  return 0;
}

size_t MockEncrypter::GetMaxPlaintextSize(size_t ciphertext_size) const {
  return ciphertext_size;
}

size_t MockEncrypter::GetCiphertextSize(size_t plaintext_size) const {
  return plaintext_size;
}

QuicStringPiece MockEncrypter::GetKey() const {
  return QuicStringPiece();
}

QuicStringPiece MockEncrypter::GetNoncePrefix() const {
  return QuicStringPiece();
}

}  // namespace net
