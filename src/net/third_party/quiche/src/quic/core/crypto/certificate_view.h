// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_
#define QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_

#include <istream>
#include <memory>
#include <vector>

#include "third_party/boringssl/src/include/openssl/base.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "net/third_party/quiche/src/quic/core/crypto/boring_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE PemReadResult {
  enum Status { kOk, kEof, kError };
  Status status;
  std::string contents;
  // The type of the PEM message (e.g., if the message starts with
  // "-----BEGIN CERTIFICATE-----", the |type| would be "CERTIFICATE").
  std::string type;
};

// Reads |input| line-by-line and returns the next available PEM message.
QUIC_EXPORT_PRIVATE PemReadResult ReadNextPemMessage(std::istream* input);

// CertificateView represents a parsed version of a single X.509 certificate. As
// the word "view" implies, it does not take ownership of the underlying strings
// and consists primarily of pointers into the certificate that is passed into
// the parser.
class QUIC_EXPORT_PRIVATE CertificateView {
 public:
  // Parses a single DER-encoded X.509 certificate.  Returns nullptr on parse
  // error.
  static std::unique_ptr<CertificateView> ParseSingleCertificate(
      quiche::QuicheStringPiece certificate);

  // Loads all PEM-encoded X.509 certificates found in the |input| stream
  // without parsing them.  Returns an empty vector if any parsing error occurs.
  static std::vector<std::string> LoadPemFromStream(std::istream* input);

  QuicWallTime validity_start() const { return validity_start_; }
  QuicWallTime validity_end() const { return validity_end_; }
  const EVP_PKEY* public_key() const { return public_key_.get(); }

  const std::vector<quiche::QuicheStringPiece>& subject_alt_name_domains()
      const {
    return subject_alt_name_domains_;
  }
  const std::vector<QuicIpAddress>& subject_alt_name_ips() const {
    return subject_alt_name_ips_;
  }

  // |signature_algorithm| is a TLS signature algorithm ID.
  bool VerifySignature(quiche::QuicheStringPiece data,
                       quiche::QuicheStringPiece signature,
                       uint16_t signature_algorithm) const;

 private:
  CertificateView() = default;

  QuicWallTime validity_start_ = QuicWallTime::Zero();
  QuicWallTime validity_end_ = QuicWallTime::Zero();

  // Public key parsed from SPKI.
  bssl::UniquePtr<EVP_PKEY> public_key_;

  // SubjectAltName, https://tools.ietf.org/html/rfc5280#section-4.2.1.6
  std::vector<quiche::QuicheStringPiece> subject_alt_name_domains_;
  std::vector<QuicIpAddress> subject_alt_name_ips_;

  // Called from ParseSingleCertificate().
  bool ParseExtensions(CBS extensions);
  bool ValidatePublicKeyParameters();
};

// CertificatePrivateKey represents a private key that can be used with an X.509
// certificate.
class QUIC_EXPORT_PRIVATE CertificatePrivateKey {
 public:
  explicit CertificatePrivateKey(bssl::UniquePtr<EVP_PKEY> private_key)
      : private_key_(std::move(private_key)) {}

  // Loads a DER-encoded PrivateKeyInfo structure (RFC 5958) as a private key.
  static std::unique_ptr<CertificatePrivateKey> LoadFromDer(
      quiche::QuicheStringPiece private_key);

  // Loads a private key from a PEM file formatted according to RFC 7468.  Also
  // supports legacy OpenSSL RSA key format ("BEGIN RSA PRIVATE KEY").
  static std::unique_ptr<CertificatePrivateKey> LoadPemFromStream(
      std::istream* input);

  // |signature_algorithm| is a TLS signature algorithm ID.
  std::string Sign(quiche::QuicheStringPiece input,
                   uint16_t signature_algorithm);

  // Verifies that the private key in question matches the public key of the
  // certificate |view|.
  bool MatchesPublicKey(const CertificateView& view);

  // Verifies that the private key can be used with the specified TLS signature
  // algorithm.
  bool ValidForSignatureAlgorithm(uint16_t signature_algorithm);

  EVP_PKEY* private_key() { return private_key_.get(); }

 private:
  CertificatePrivateKey() = default;

  bssl::UniquePtr<EVP_PKEY> private_key_;
};

// Parses a DER time based on the specified ASN.1 tag.  Exposed primarily for
// testing.
QUIC_EXPORT_PRIVATE quiche::QuicheOptional<quic::QuicWallTime> ParseDerTime(
    unsigned tag,
    quiche::QuicheStringPiece payload);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_
