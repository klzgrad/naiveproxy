// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_
#define QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_

#include <istream>
#include <memory>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "openssl/base.h"
#include "openssl/bytestring.h"
#include "openssl/evp.h"
#include "quiche/quic/core/crypto/boring_utils.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_ip_address.h"

namespace quic {

struct QUICHE_EXPORT PemReadResult {
  enum Status { kOk, kEof, kError };
  Status status;
  std::string contents;
  // The type of the PEM message (e.g., if the message starts with
  // "-----BEGIN CERTIFICATE-----", the |type| would be "CERTIFICATE").
  std::string type;
};

// Reads |input| line-by-line and returns the next available PEM message.
QUICHE_EXPORT PemReadResult ReadNextPemMessage(std::istream* input);

// Cryptograhpic algorithms recognized in X.509.
enum class PublicKeyType {
  kRsa,
  kP256,
  kP384,
  kEd25519,
  kUnknown,
};
QUICHE_EXPORT std::string PublicKeyTypeToString(PublicKeyType type);
QUICHE_EXPORT PublicKeyType
PublicKeyTypeFromSignatureAlgorithm(uint16_t signature_algorithm);

// Returns the list of the signature algorithms that can be processed by
// CertificateView::VerifySignature() and CertificatePrivateKey::Sign().
QUICHE_EXPORT QuicSignatureAlgorithmVector
SupportedSignatureAlgorithmsForQuic();

// CertificateView represents a parsed version of a single X.509 certificate. As
// the word "view" implies, it does not take ownership of the underlying strings
// and consists primarily of pointers into the certificate that is passed into
// the parser.
class QUICHE_EXPORT CertificateView {
 public:
  // Parses a single DER-encoded X.509 certificate.  Returns nullptr on parse
  // error.
  static std::unique_ptr<CertificateView> ParseSingleCertificate(
      absl::string_view certificate);

  // Loads all PEM-encoded X.509 certificates found in the |input| stream
  // without parsing them.  Returns an empty vector if any parsing error occurs.
  static std::vector<std::string> LoadPemFromStream(std::istream* input);

  QuicWallTime validity_start() const { return validity_start_; }
  QuicWallTime validity_end() const { return validity_end_; }
  const EVP_PKEY* public_key() const { return public_key_.get(); }

  const std::vector<absl::string_view>& subject_alt_name_domains() const {
    return subject_alt_name_domains_;
  }
  const std::vector<QuicIpAddress>& subject_alt_name_ips() const {
    return subject_alt_name_ips_;
  }

  // Returns a human-readable representation of the Subject field.  The format
  // is similar to RFC 2253, but does not match it exactly.
  absl::optional<std::string> GetHumanReadableSubject() const;

  // |signature_algorithm| is a TLS signature algorithm ID.
  bool VerifySignature(absl::string_view data, absl::string_view signature,
                       uint16_t signature_algorithm) const;

  // Returns the type of the key used in the certificate's SPKI.
  PublicKeyType public_key_type() const;

 private:
  CertificateView() = default;

  QuicWallTime validity_start_ = QuicWallTime::Zero();
  QuicWallTime validity_end_ = QuicWallTime::Zero();
  absl::string_view subject_der_;

  // Public key parsed from SPKI.
  bssl::UniquePtr<EVP_PKEY> public_key_;

  // SubjectAltName, https://tools.ietf.org/html/rfc5280#section-4.2.1.6
  std::vector<absl::string_view> subject_alt_name_domains_;
  std::vector<QuicIpAddress> subject_alt_name_ips_;

  // Called from ParseSingleCertificate().
  bool ParseExtensions(CBS extensions);
  bool ValidatePublicKeyParameters();
};

// CertificatePrivateKey represents a private key that can be used with an X.509
// certificate.
class QUICHE_EXPORT CertificatePrivateKey {
 public:
  explicit CertificatePrivateKey(bssl::UniquePtr<EVP_PKEY> private_key)
      : private_key_(std::move(private_key)) {}

  // Loads a DER-encoded PrivateKeyInfo structure (RFC 5958) as a private key.
  static std::unique_ptr<CertificatePrivateKey> LoadFromDer(
      absl::string_view private_key);

  // Loads a private key from a PEM file formatted according to RFC 7468.  Also
  // supports legacy OpenSSL RSA key format ("BEGIN RSA PRIVATE KEY").
  static std::unique_ptr<CertificatePrivateKey> LoadPemFromStream(
      std::istream* input);

  // |signature_algorithm| is a TLS signature algorithm ID.
  std::string Sign(absl::string_view input, uint16_t signature_algorithm) const;

  // Verifies that the private key in question matches the public key of the
  // certificate |view|.
  bool MatchesPublicKey(const CertificateView& view) const;

  // Verifies that the private key can be used with the specified TLS signature
  // algorithm.
  bool ValidForSignatureAlgorithm(uint16_t signature_algorithm) const;

  EVP_PKEY* private_key() const { return private_key_.get(); }

 private:
  CertificatePrivateKey() = default;

  bssl::UniquePtr<EVP_PKEY> private_key_;
};

// Parses a DER-encoded X.509 NameAttribute.  Exposed primarily for testing.
QUICHE_EXPORT absl::optional<std::string> X509NameAttributeToString(CBS input);

// Parses a DER time based on the specified ASN.1 tag.  Exposed primarily for
// testing.
QUICHE_EXPORT absl::optional<quic::QuicWallTime> ParseDerTime(
    unsigned tag, absl::string_view payload);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_
