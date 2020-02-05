// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTER_H_

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

namespace quic {

// QuicCrypter is the parent class for QuicEncrypter and QuicDecrypter.
// Its purpose is to provide an interface for using methods that are common to
// both classes when operations are being done that apply to both encrypters and
// decrypters.
class QUIC_EXPORT_PRIVATE QuicCrypter {
 public:
  virtual ~QuicCrypter() {}

  // Sets the symmetric encryption/decryption key. Returns true on success,
  // false on failure.
  //
  // NOTE: The key is the client_write_key or server_write_key derived from
  // the master secret.
  virtual bool SetKey(QuicStringPiece key) = 0;

  // Sets the fixed initial bytes of the nonce. Returns true on success,
  // false on failure. This method must only be used with Google QUIC crypters.
  //
  // NOTE: The nonce prefix is the client_write_iv or server_write_iv
  // derived from the master secret. A 64-bit packet number will
  // be appended to form the nonce.
  //
  //                          <------------ 64 bits ----------->
  //   +---------------------+----------------------------------+
  //   |    Fixed prefix     |      packet number      |
  //   +---------------------+----------------------------------+
  //                          Nonce format
  //
  // The security of the nonce format requires that QUIC never reuse a
  // packet number, even when retransmitting a lost packet.
  virtual bool SetNoncePrefix(QuicStringPiece nonce_prefix) = 0;

  // Sets |iv| as the initialization vector to use when constructing the nonce.
  // Returns true on success, false on failure. This method must only be used
  // with IETF QUIC crypters.
  //
  // Google QUIC and IETF QUIC use different nonce constructions. This method
  // must be used when using IETF QUIC; SetNoncePrefix must be used when using
  // Google QUIC.
  //
  // The nonce is constructed as follows (draft-ietf-quic-tls-14 section 5.2):
  //
  //    <---------------- max(8, N_MIN) bytes ----------------->
  //   +--------------------------------------------------------+
  //   |                 packet protection IV                   |
  //   +--------------------------------------------------------+
  //                             XOR
  //                          <------------ 64 bits ----------->
  //   +---------------------+----------------------------------+
  //   |        zeroes       |   reconstructed packet number    |
  //   +---------------------+----------------------------------+
  //
  // The nonce is the packet protection IV (|iv|) XOR'd with the left-padded
  // reconstructed packet number.
  //
  // The security of the nonce format requires that QUIC never reuse a
  // packet number, even when retransmitting a lost packet.
  virtual bool SetIV(QuicStringPiece iv) = 0;

  // Calls SetNoncePrefix or SetIV depending on whether |version| uses the
  // Google QUIC crypto or IETF QUIC nonce construction.
  virtual bool SetNoncePrefixOrIV(const ParsedQuicVersion& version,
                                  QuicStringPiece nonce_prefix_or_iv);

  // Sets the key to use for header protection.
  virtual bool SetHeaderProtectionKey(QuicStringPiece key) = 0;

  // GetKeySize, GetIVSize, and GetNoncePrefixSize are used to know how many
  // bytes of key material needs to be derived from the master secret.

  // Returns the size in bytes of a key for the algorithm.
  virtual size_t GetKeySize() const = 0;
  // Returns the size in bytes of an IV to use with the algorithm.
  virtual size_t GetIVSize() const = 0;
  // Returns the size in bytes of the fixed initial part of the nonce.
  virtual size_t GetNoncePrefixSize() const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_QUIC_CRYPTER_H_
