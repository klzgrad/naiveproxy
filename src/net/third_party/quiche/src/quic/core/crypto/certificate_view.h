// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_
#define QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_

#include <memory>

#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "net/third_party/quiche/src/quic/core/crypto/boring_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

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

  EVP_PKEY* public_key() { return public_key_.get(); }

  const std::vector<quiche::QuicheStringPiece>& subject_alt_name_domains()
      const {
    return subject_alt_name_domains_;
  }
  const std::vector<QuicIpAddress>& subject_alt_name_ips() const {
    return subject_alt_name_ips_;
  }

 private:
  CertificateView() = default;

  // Public key parsed from SPKI.
  bssl::UniquePtr<EVP_PKEY> public_key_;

  // SubjectAltName, https://tools.ietf.org/html/rfc5280#section-4.2.1.6
  std::vector<quiche::QuicheStringPiece> subject_alt_name_domains_;
  std::vector<QuicIpAddress> subject_alt_name_ips_;

  // Called from ParseSingleCertificate().
  bool ParseExtensions(CBS extensions);
  bool ValidatePublicKeyParameters();
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_VIEW_H_
