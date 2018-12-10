// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/mock_decrypter.h"

#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"

using quic::DiversificationNonce;
using quic::Perspective;
using quic::QuicPacketNumber;
using quic::QuicStringPiece;
using quic::QuicTransportVersion;

namespace net {

MockDecrypter::MockDecrypter(Perspective perspective) {}

bool MockDecrypter::SetKey(QuicStringPiece key) {
  return key.empty();
}

bool MockDecrypter::SetNoncePrefix(QuicStringPiece nonce_prefix) {
  return nonce_prefix.empty();
}

bool MockDecrypter::SetIV(QuicStringPiece iv) {
  return iv.empty();
}

bool MockDecrypter::SetPreliminaryKey(QuicStringPiece key) {
  QUIC_BUG << "Should not be called";
  return false;
}

bool MockDecrypter::SetDiversificationNonce(const DiversificationNonce& nonce) {
  QUIC_BUG << "Should not be called";
  return true;
}

bool MockDecrypter::DecryptPacket(QuicTransportVersion version,
                                  QuicPacketNumber /*packet_number*/,
                                  QuicStringPiece associated_data,
                                  QuicStringPiece ciphertext,
                                  char* output,
                                  size_t* output_length,
                                  size_t max_output_length) {
  if (ciphertext.length() > max_output_length) {
    return false;
  }

  memcpy(output, ciphertext.data(), ciphertext.length());
  *output_length = ciphertext.length();
  return true;
}

size_t MockDecrypter::GetKeySize() const {
  return 0;
}

size_t MockDecrypter::GetIVSize() const {
  return 0;
}

QuicStringPiece MockDecrypter::GetKey() const {
  return QuicStringPiece();
}

QuicStringPiece MockDecrypter::GetNoncePrefix() const {
  return QuicStringPiece();
}

uint32_t MockDecrypter::cipher_id() const {
  return 0;
}

}  // namespace net
