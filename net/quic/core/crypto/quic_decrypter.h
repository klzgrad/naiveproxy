// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_QUIC_DECRYPTER_H_
#define NET_QUIC_CORE_CRYPTO_QUIC_DECRYPTER_H_

#include <cstddef>
#include <cstdint>

#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicDecrypter {
 public:
  virtual ~QuicDecrypter() {}

  static QuicDecrypter* Create(QuicTag algorithm);

  // Sets the encryption key. Returns true on success, false on failure.
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
  // The nonce is constructed as follows (draft-ietf-quic-tls section 5.3):
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

  // Sets the encryption key. Returns true on success, false on failure.
  // |DecryptPacket| may not be called until |SetDiversificationNonce| is
  // called and the preliminary keying material will be combined with that
  // nonce in order to create the actual key and nonce-prefix.
  //
  // If this function is called, neither |SetKey| nor |SetNoncePrefix| may be
  // called.
  virtual bool SetPreliminaryKey(QuicStringPiece key) = 0;

  // SetDiversificationNonce uses |nonce| to derive final keys based on the
  // input keying material given by calling |SetPreliminaryKey|.
  //
  // Calling this function is a no-op if |SetPreliminaryKey| hasn't been
  // called.
  virtual bool SetDiversificationNonce(const DiversificationNonce& nonce) = 0;

  // Populates |output| with the decrypted |ciphertext| and populates
  // |output_length| with the length.  Returns 0 if there is an error.
  // |output| size is specified by |max_output_length| and must be
  // at least as large as the ciphertext.  |packet_number| is
  // appended to the |nonce_prefix| value provided in SetNoncePrefix()
  // to form the nonce.
  // TODO(wtc): add a way for DecryptPacket to report decryption failure due
  // to non-authentic inputs, as opposed to other reasons for failure.
  virtual bool DecryptPacket(QuicTransportVersion version,
                             QuicPacketNumber packet_number,
                             QuicStringPiece associated_data,
                             QuicStringPiece ciphertext,
                             char* output,
                             size_t* output_length,
                             size_t max_output_length) = 0;

  // The ID of the cipher. Return 0x03000000 ORed with the 'cryptographic suite
  // selector'.
  virtual uint32_t cipher_id() const = 0;

  // For use by unit tests only.
  virtual QuicStringPiece GetKey() const = 0;
  virtual QuicStringPiece GetNoncePrefix() const = 0;

  static void DiversifyPreliminaryKey(QuicStringPiece preliminary_key,
                                      QuicStringPiece nonce_prefix,
                                      const DiversificationNonce& nonce,
                                      size_t key_size,
                                      size_t nonce_prefix_size,
                                      std::string* out_key,
                                      std::string* out_nonce_prefix);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_QUIC_DECRYPTER_H_
