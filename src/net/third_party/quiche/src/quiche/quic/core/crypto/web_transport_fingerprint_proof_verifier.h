// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_WEB_TRANSPORT_FINGERPRINT_PROOF_VERIFIER_H_
#define QUICHE_QUIC_CORE_CRYPTO_WEB_TRANSPORT_FINGERPRINT_PROOF_VERIFIER_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Represents a fingerprint of an X.509 certificate in a format based on
// https://w3c.github.io/webrtc-pc/#dom-rtcdtlsfingerprint.
// TODO(vasilvv): remove this once all consumers of this API use
// WebTransportHash.
struct QUICHE_EXPORT CertificateFingerprint {
  static constexpr char kSha256[] = "sha-256";

  // An algorithm described by one of the names in
  // https://www.iana.org/assignments/hash-function-text-names/hash-function-text-names.xhtml
  std::string algorithm;
  // Hex-encoded, colon-separated fingerprint of the certificate.  For example,
  // "12:3d:5b:71:8c:54:df:85:7e:bd:e3:7c:66:da:f9:db:6a:94:8f:85:cb:6e:44:7f:09:3e:05:f2:dd:d4:f7:86"
  std::string fingerprint;
};

// Represents a fingerprint of an X.509 certificate in a format based on
// https://w3c.github.io/webtransport/#dictdef-webtransporthash.
struct QUICHE_EXPORT WebTransportHash {
  static constexpr char kSha256[] = "sha-256";

  // An algorithm described by one of the names in
  // https://www.iana.org/assignments/hash-function-text-names/hash-function-text-names.xhtml
  std::string algorithm;
  // Raw bytes of the hash.
  std::string value;
};

// WebTransportFingerprintProofVerifier verifies the server leaf certificate
// against a supplied list of certificate fingerprints following the procedure
// described in the WebTransport specification.  The certificate is deemed
// trusted if it matches a fingerprint in the list, has expiry dates that are
// not too long and has not expired.  Only the leaf is checked, the rest of the
// chain is ignored. Reference specification:
// https://wicg.github.io/web-transport/#dom-quictransportconfiguration-server_certificate_fingerprints
class QUICHE_EXPORT WebTransportFingerprintProofVerifier
    : public ProofVerifier {
 public:
  // Note: the entries in this list may be logged into a UMA histogram, and thus
  // should not be renumbered.
  enum class Status {
    kValidCertificate = 0,
    kUnknownFingerprint = 1,
    kCertificateParseFailure = 2,
    kExpiryTooLong = 3,
    kExpired = 4,
    kInternalError = 5,
    kDisallowedKeyAlgorithm = 6,

    kMaxValue = kDisallowedKeyAlgorithm,
  };

  class QUICHE_EXPORT Details : public ProofVerifyDetails {
   public:
    explicit Details(Status status) : status_(status) {}
    Status status() const { return status_; }

    ProofVerifyDetails* Clone() const override;

   private:
    const Status status_;
  };

  // |clock| is used to check if the certificate has expired.  It is not owned
  // and must outlive the object.  |max_validity_days| is the maximum time for
  // which the certificate is allowed to be valid.
  WebTransportFingerprintProofVerifier(const QuicClock* clock,
                                       int max_validity_days);

  // Adds a certificate fingerprint to be trusted.  The fingerprints are
  // case-insensitive and are validated internally; the function returns true if
  // the validation passes.
  bool AddFingerprint(CertificateFingerprint fingerprint);
  bool AddFingerprint(WebTransportHash hash);

  // ProofVerifier implementation.
  QuicAsyncStatus VerifyProof(
      const std::string& hostname, const uint16_t port,
      const std::string& server_config, QuicTransportVersion transport_version,
      absl::string_view chlo_hash, const std::vector<std::string>& certs,
      const std::string& cert_sct, const std::string& signature,
      const ProofVerifyContext* context, std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override;
  QuicAsyncStatus VerifyCertChain(
      const std::string& hostname, const uint16_t port,
      const std::vector<std::string>& certs, const std::string& ocsp_response,
      const std::string& cert_sct, const ProofVerifyContext* context,
      std::string* error_details, std::unique_ptr<ProofVerifyDetails>* details,
      uint8_t* out_alert,
      std::unique_ptr<ProofVerifierCallback> callback) override;
  std::unique_ptr<ProofVerifyContext> CreateDefaultContext() override;

 protected:
  virtual bool IsKeyTypeAllowedByPolicy(const CertificateView& certificate);

 private:
  bool HasKnownFingerprint(absl::string_view der_certificate);
  bool HasValidExpiry(const CertificateView& certificate);
  bool IsWithinValidityPeriod(const CertificateView& certificate);

  const QuicClock* clock_;  // Unowned.
  const int max_validity_days_;
  const QuicTime::Delta max_validity_;
  std::vector<WebTransportHash> hashes_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_WEB_TRANSPORT_FINGERPRINT_PROOF_VERIFIER_H_
