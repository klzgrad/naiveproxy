// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Some helpers for quic crypto

#ifndef QUICHE_QUIC_CORE_CRYPTO_CRYPTO_UTILS_H_
#define QUICHE_QUIC_CORE_CRYPTO_CRYPTO_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "third_party/boringssl/src/include/openssl/evp.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypter.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuicRandom;

class QUIC_EXPORT_PRIVATE CryptoUtils {
 public:
  CryptoUtils() = delete;

  // Diversification is a utility class that's used to act like a union type.
  // Values can be created by calling the functions like |NoDiversification|,
  // below.
  class QUIC_EXPORT_PRIVATE Diversification {
   public:
    enum Mode {
      NEVER,  // Key diversification will never be used. Forward secure
              // crypters will always use this mode.

      PENDING,  // Key diversification will happen when a nonce is later
                // received. This should only be used by clients initial
                // decrypters which are waiting on the divesification nonce
                // from the server.

      NOW,  // Key diversification will happen immediate based on the nonce.
            // This should only be used by servers initial encrypters.
    };

    Diversification(const Diversification& diversification) = default;

    static Diversification Never() { return Diversification(NEVER, nullptr); }
    static Diversification Pending() {
      return Diversification(PENDING, nullptr);
    }
    static Diversification Now(DiversificationNonce* nonce) {
      return Diversification(NOW, nonce);
    }

    Mode mode() const { return mode_; }
    DiversificationNonce* nonce() const {
      DCHECK_EQ(mode_, NOW);
      return nonce_;
    }

   private:
    Diversification(Mode mode, DiversificationNonce* nonce)
        : mode_(mode), nonce_(nonce) {}

    Mode mode_;
    DiversificationNonce* nonce_;
  };

  // SetKeyAndIV derives the key and IV from the given packet protection secret
  // |pp_secret| and sets those fields on the given QuicCrypter |*crypter|.
  // This follows the derivation described in section 7.3 of RFC 8446, except
  // with the label prefix in HKDF-Expand-Label changed from "tls13 " to "quic "
  // as described in draft-ietf-quic-tls-14, section 5.1.
  static void SetKeyAndIV(const EVP_MD* prf,
                          const std::vector<uint8_t>& pp_secret,
                          QuicCrypter* crypter);

  // IETF QUIC encrypts ENCRYPTION_INITIAL messages with a version-specific key
  // (to prevent network observers that are not aware of that QUIC version from
  // making decisions based on the TLS handshake). This packet protection secret
  // is derived from the connection ID in the client's Initial packet.
  //
  // This function takes that |connection_id| and creates the encrypter and
  // decrypter (put in |*crypters|) to use for this packet protection, as well
  // as setting the key and IV on those crypters. For older versions of QUIC
  // that do not use the new IETF style ENCRYPTION_INITIAL obfuscators, this
  // function puts a NullEncrypter and NullDecrypter in |*crypters|.
  static void CreateInitialObfuscators(Perspective perspective,
                                       ParsedQuicVersion version,
                                       QuicConnectionId connection_id,
                                       CrypterPair* crypters);

  // IETF QUIC Retry packets carry a retry integrity tag to detect packet
  // corruption and make it harder for an attacker to spoof. This function
  // checks whether a given retry packet is valid.
  static bool ValidateRetryIntegrityTag(
      ParsedQuicVersion version,
      QuicConnectionId original_connection_id,
      quiche::QuicheStringPiece retry_without_tag,
      quiche::QuicheStringPiece integrity_tag);

  // Generates the connection nonce. The nonce is formed as:
  //   <4 bytes> current time
  //   <8 bytes> |orbit| (or random if |orbit| is empty)
  //   <20 bytes> random
  static void GenerateNonce(QuicWallTime now,
                            QuicRandom* random_generator,
                            quiche::QuicheStringPiece orbit,
                            std::string* nonce);

  // DeriveKeys populates |crypters->encrypter|, |crypters->decrypter|, and
  // |subkey_secret| (optional -- may be null) given the contents of
  // |premaster_secret|, |client_nonce|, |server_nonce| and |hkdf_input|. |aead|
  // determines which cipher will be used. |perspective| controls whether the
  // server's keys are assigned to |encrypter| or |decrypter|. |server_nonce| is
  // optional and, if non-empty, is mixed into the key derivation.
  // |subkey_secret| will have the same length as |premaster_secret|.
  //
  // If |pre_shared_key| is non-empty, it is incorporated into the key
  // derivation parameters.  If it is empty, the key derivation is unaltered.
  //
  // If the mode of |diversification| is NEVER, the the crypters will be
  // configured to never perform key diversification. If the mode is
  // NOW (which is only for servers, then the encrypter will be keyed via a
  // two-step process that uses the nonce from |diversification|.
  // If the mode is PENDING (which is only for servres), then the
  // decrypter will only be keyed to a preliminary state: a call to
  // |SetDiversificationNonce| with a diversification nonce will be needed to
  // complete keying.
  static bool DeriveKeys(const ParsedQuicVersion& version,
                         quiche::QuicheStringPiece premaster_secret,
                         QuicTag aead,
                         quiche::QuicheStringPiece client_nonce,
                         quiche::QuicheStringPiece server_nonce,
                         quiche::QuicheStringPiece pre_shared_key,
                         const std::string& hkdf_input,
                         Perspective perspective,
                         Diversification diversification,
                         CrypterPair* crypters,
                         std::string* subkey_secret);

  // Performs key extraction to derive a new secret of |result_len| bytes
  // dependent on |subkey_secret|, |label|, and |context|. Returns false if the
  // parameters are invalid (e.g. |label| contains null bytes); returns true on
  // success.
  static bool ExportKeyingMaterial(quiche::QuicheStringPiece subkey_secret,
                                   quiche::QuicheStringPiece label,
                                   quiche::QuicheStringPiece context,
                                   size_t result_len,
                                   std::string* result);

  // Computes the FNV-1a hash of the provided DER-encoded cert for use in the
  // XLCT tag.
  static uint64_t ComputeLeafCertHash(quiche::QuicheStringPiece cert);

  // Validates that |server_hello| is actually an SHLO message and that it is
  // not part of a downgrade attack.
  //
  // Returns QUIC_NO_ERROR if this is the case or returns the appropriate error
  // code and sets |error_details|.
  static QuicErrorCode ValidateServerHello(
      const CryptoHandshakeMessage& server_hello,
      const ParsedQuicVersionVector& negotiated_versions,
      std::string* error_details);

  // Validates that the |server_versions| received do not indicate that the
  // ServerHello is part of a downgrade attack. |negotiated_versions| must
  // contain the list of versions received in the server's version negotiation
  // packet (or be empty if no such packet was received).
  //
  // Returns QUIC_NO_ERROR if this is the case or returns the appropriate error
  // code and sets |error_details|.
  static QuicErrorCode ValidateServerHelloVersions(
      const QuicVersionLabelVector& server_versions,
      const ParsedQuicVersionVector& negotiated_versions,
      std::string* error_details);

  // Validates that |client_hello| is actually a CHLO and that this is not part
  // of a downgrade attack.
  // This includes verifiying versions and detecting downgrade attacks.
  //
  // Returns QUIC_NO_ERROR if this is the case or returns the appropriate error
  // code and sets |error_details|.
  static QuicErrorCode ValidateClientHello(
      const CryptoHandshakeMessage& client_hello,
      ParsedQuicVersion version,
      const ParsedQuicVersionVector& supported_versions,
      std::string* error_details);

  // Validates that the |client_version| received does not indicate that a
  // downgrade attack has occurred. |connection_version| is the version of the
  // QuicConnection, and |supported_versions| is all versions that that
  // QuicConnection supports.
  //
  // Returns QUIC_NO_ERROR if this is the case or returns the appropriate error
  // code and sets |error_details|.
  static QuicErrorCode ValidateClientHelloVersion(
      QuicVersionLabel client_version,
      ParsedQuicVersion connection_version,
      const ParsedQuicVersionVector& supported_versions,
      std::string* error_details);

  // Returns the name of the HandshakeFailureReason as a char*
  static const char* HandshakeFailureReasonToString(
      HandshakeFailureReason reason);

  // Returns a hash of the serialized |message|.
  static std::string HashHandshakeMessage(const CryptoHandshakeMessage& message,
                                          Perspective perspective);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CRYPTO_UTILS_H_
