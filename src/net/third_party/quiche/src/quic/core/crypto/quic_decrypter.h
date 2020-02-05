// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_QUIC_DECRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_QUIC_DECRYPTER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/quic_crypter.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicDecrypter : public QuicCrypter {
 public:
  virtual ~QuicDecrypter() {}

  static std::unique_ptr<QuicDecrypter> Create(const ParsedQuicVersion& version,
                                               QuicTag algorithm);

  // Creates an IETF QuicDecrypter based on |cipher_suite| which must be an id
  // returned by SSL_CIPHER_get_id. The caller is responsible for taking
  // ownership of the new QuicDecrypter.
  static std::unique_ptr<QuicDecrypter> CreateFromCipherSuite(
      uint32_t cipher_suite);

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
  virtual bool DecryptPacket(uint64_t packet_number,
                             QuicStringPiece associated_data,
                             QuicStringPiece ciphertext,
                             char* output,
                             size_t* output_length,
                             size_t max_output_length) = 0;

  // Reads a sample of ciphertext from |sample_reader| and uses the header
  // protection key to generate a mask to use for header protection. If
  // successful, this function returns this mask, which is at least 5 bytes
  // long. Callers can detect failure by checking if the output string is empty.
  virtual std::string GenerateHeaderProtectionMask(
      QuicDataReader* sample_reader) = 0;

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

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_QUIC_DECRYPTER_H_
