// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/web_transport_fingerprint_proof_verifier.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "openssl/sha.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {
namespace {

constexpr size_t kFingerprintLength = SHA256_DIGEST_LENGTH * 3 - 1;

// Assumes that the character is normalized to lowercase beforehand.
bool IsNormalizedHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

void NormalizeFingerprint(CertificateFingerprint& fingerprint) {
  fingerprint.fingerprint =
      quiche::QuicheTextUtils::ToLower(fingerprint.fingerprint);
}

}  // namespace

constexpr char CertificateFingerprint::kSha256[];
constexpr char WebTransportHash::kSha256[];

ProofVerifyDetails* WebTransportFingerprintProofVerifier::Details::Clone()
    const {
  return new Details(*this);
}

WebTransportFingerprintProofVerifier::WebTransportFingerprintProofVerifier(
    const QuicClock* clock, int max_validity_days)
    : clock_(clock),
      max_validity_days_(max_validity_days),
      // Add an extra second to max validity to accomodate various edge cases.
      max_validity_(
          QuicTime::Delta::FromSeconds(max_validity_days * 86400 + 1)) {}

bool WebTransportFingerprintProofVerifier::AddFingerprint(
    CertificateFingerprint fingerprint) {
  NormalizeFingerprint(fingerprint);
  if (!absl::EqualsIgnoreCase(fingerprint.algorithm,
                              CertificateFingerprint::kSha256)) {
    QUIC_DLOG(WARNING) << "Algorithms other than SHA-256 are not supported";
    return false;
  }
  if (fingerprint.fingerprint.size() != kFingerprintLength) {
    QUIC_DLOG(WARNING) << "Invalid fingerprint length";
    return false;
  }
  for (size_t i = 0; i < fingerprint.fingerprint.size(); i++) {
    char current = fingerprint.fingerprint[i];
    if (i % 3 == 2) {
      if (current != ':') {
        QUIC_DLOG(WARNING)
            << "Missing colon separator between the bytes of the hash";
        return false;
      }
    } else {
      if (!IsNormalizedHexDigit(current)) {
        QUIC_DLOG(WARNING) << "Fingerprint must be in hexadecimal";
        return false;
      }
    }
  }

  std::string normalized =
      absl::StrReplaceAll(fingerprint.fingerprint, {{":", ""}});
  std::string normalized_bytes;
  if (!absl::HexStringToBytes(normalized, &normalized_bytes)) {
    QUIC_DLOG(WARNING) << "Fingerprint hexadecimal is invalid";
    return false;
  }
  hashes_.push_back(
      WebTransportHash{fingerprint.algorithm, std::move(normalized_bytes)});
  return true;
}

bool WebTransportFingerprintProofVerifier::AddFingerprint(
    WebTransportHash hash) {
  if (hash.algorithm != CertificateFingerprint::kSha256) {
    QUIC_DLOG(WARNING) << "Algorithms other than SHA-256 are not supported";
    return false;
  }
  if (hash.value.size() != SHA256_DIGEST_LENGTH) {
    QUIC_DLOG(WARNING) << "Invalid fingerprint length";
    return false;
  }
  hashes_.push_back(std::move(hash));
  return true;
}

QuicAsyncStatus WebTransportFingerprintProofVerifier::VerifyProof(
    const std::string& /*hostname*/, const uint16_t /*port*/,
    const std::string& /*server_config*/,
    QuicTransportVersion /*transport_version*/, absl::string_view /*chlo_hash*/,
    const std::vector<std::string>& /*certs*/, const std::string& /*cert_sct*/,
    const std::string& /*signature*/, const ProofVerifyContext* /*context*/,
    std::string* error_details, std::unique_ptr<ProofVerifyDetails>* details,
    std::unique_ptr<ProofVerifierCallback> /*callback*/) {
  *error_details =
      "QUIC crypto certificate verification is not supported in "
      "WebTransportFingerprintProofVerifier";
  QUIC_BUG(quic_bug_10879_1) << *error_details;
  *details = std::make_unique<Details>(Status::kInternalError);
  return QUIC_FAILURE;
}

QuicAsyncStatus WebTransportFingerprintProofVerifier::VerifyCertChain(
    const std::string& /*hostname*/, const uint16_t /*port*/,
    const std::vector<std::string>& certs, const std::string& /*ocsp_response*/,
    const std::string& /*cert_sct*/, const ProofVerifyContext* /*context*/,
    std::string* error_details, std::unique_ptr<ProofVerifyDetails>* details,
    uint8_t* /*out_alert*/,
    std::unique_ptr<ProofVerifierCallback> /*callback*/) {
  if (certs.empty()) {
    *details = std::make_unique<Details>(Status::kInternalError);
    *error_details = "No certificates provided";
    return QUIC_FAILURE;
  }

  if (!HasKnownFingerprint(certs[0])) {
    *details = std::make_unique<Details>(Status::kUnknownFingerprint);
    *error_details = "Certificate does not match any fingerprint";
    return QUIC_FAILURE;
  }

  std::unique_ptr<CertificateView> view =
      CertificateView::ParseSingleCertificate(certs[0]);
  if (view == nullptr) {
    *details = std::make_unique<Details>(Status::kCertificateParseFailure);
    *error_details = "Failed to parse the certificate";
    return QUIC_FAILURE;
  }

  if (!HasValidExpiry(*view)) {
    *details = std::make_unique<Details>(Status::kExpiryTooLong);
    *error_details =
        absl::StrCat("Certificate expiry exceeds the configured limit of ",
                     max_validity_days_, " days");
    return QUIC_FAILURE;
  }

  if (!IsWithinValidityPeriod(*view)) {
    *details = std::make_unique<Details>(Status::kExpired);
    *error_details =
        "Certificate has expired or has validity listed in the future";
    return QUIC_FAILURE;
  }

  if (!IsKeyTypeAllowedByPolicy(*view)) {
    *details = std::make_unique<Details>(Status::kDisallowedKeyAlgorithm);
    *error_details =
        absl::StrCat("Certificate uses a disallowed public key type (",
                     PublicKeyTypeToString(view->public_key_type()), ")");
    return QUIC_FAILURE;
  }

  *details = std::make_unique<Details>(Status::kValidCertificate);
  return QUIC_SUCCESS;
}

std::unique_ptr<ProofVerifyContext>
WebTransportFingerprintProofVerifier::CreateDefaultContext() {
  return nullptr;
}

bool WebTransportFingerprintProofVerifier::HasKnownFingerprint(
    absl::string_view der_certificate) {
  // https://w3c.github.io/webtransport/#verify-a-certificate-hash
  const std::string hash = RawSha256(der_certificate);
  for (const WebTransportHash& reference : hashes_) {
    if (reference.algorithm != WebTransportHash::kSha256) {
      QUIC_BUG(quic_bug_10879_2) << "Unexpected non-SHA-256 hash";
      continue;
    }
    if (hash == reference.value) {
      return true;
    }
  }
  return false;
}

bool WebTransportFingerprintProofVerifier::HasValidExpiry(
    const CertificateView& certificate) {
  if (!certificate.validity_start().IsBefore(certificate.validity_end())) {
    return false;
  }

  const QuicTime::Delta duration_seconds =
      certificate.validity_end() - certificate.validity_start();
  return duration_seconds <= max_validity_;
}

bool WebTransportFingerprintProofVerifier::IsWithinValidityPeriod(
    const CertificateView& certificate) {
  QuicWallTime now = clock_->WallNow();
  return now.IsAfter(certificate.validity_start()) &&
         now.IsBefore(certificate.validity_end());
}

bool WebTransportFingerprintProofVerifier::IsKeyTypeAllowedByPolicy(
    const CertificateView& certificate) {
  switch (certificate.public_key_type()) {
    // https://github.com/w3c/webtransport/pull/375 defines P-256 as an MTI
    // algorithm, and prohibits RSA.  We also allow P-384 and Ed25519.
    case PublicKeyType::kP256:
    case PublicKeyType::kP384:
    case PublicKeyType::kEd25519:
      return true;
    case PublicKeyType::kRsa:
      // TODO(b/213614428): this should be false by default.
      return true;
    default:
      return false;
  }
}

}  // namespace quic
