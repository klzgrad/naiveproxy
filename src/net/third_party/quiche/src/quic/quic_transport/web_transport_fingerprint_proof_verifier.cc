// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quic_transport/web_transport_fingerprint_proof_verifier.h"

#include <cstdint>
#include <memory>

#include "third_party/boringssl/src/include/openssl/sha.h"
#include "net/third_party/quiche/src/quic/core/crypto/certificate_view.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {
namespace {

constexpr size_t kFingerprintLength = SHA256_DIGEST_LENGTH * 3 - 1;

constexpr std::array<char, 16> kHexDigits = {'0', '1', '2', '3', '4', '5',
                                             '6', '7', '8', '9', 'a', 'b',
                                             'c', 'd', 'e', 'f'};

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

std::string ComputeSha256Fingerprint(quiche::QuicheStringPiece input) {
  std::vector<uint8_t> raw_hash;
  raw_hash.resize(SHA256_DIGEST_LENGTH);
  SHA256(reinterpret_cast<const uint8_t*>(input.data()), input.size(),
         raw_hash.data());

  std::string output;
  output.resize(kFingerprintLength);
  for (size_t i = 0; i < output.size(); i++) {
    uint8_t hash_byte = raw_hash[i / 3];
    switch (i % 3) {
      case 0:
        output[i] = kHexDigits[hash_byte >> 4];
        break;
      case 1:
        output[i] = kHexDigits[hash_byte & 0xf];
        break;
      case 2:
        output[i] = ':';
        break;
    }
  }
  return output;
}

ProofVerifyDetails* WebTransportFingerprintProofVerifier::Details::Clone()
    const {
  return new Details(*this);
}

WebTransportFingerprintProofVerifier::WebTransportFingerprintProofVerifier(
    const QuicClock* clock,
    int max_validity_days)
    : clock_(clock),
      max_validity_days_(max_validity_days),
      // Add an extra second to max validity to accomodate various edge cases.
      max_validity_(
          QuicTime::Delta::FromSeconds(max_validity_days * 86400 + 1)) {}

bool WebTransportFingerprintProofVerifier::AddFingerprint(
    CertificateFingerprint fingerprint) {
  NormalizeFingerprint(fingerprint);
  if (fingerprint.algorithm != CertificateFingerprint::kSha256) {
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

  fingerprints_.push_back(fingerprint);
  return true;
}

QuicAsyncStatus WebTransportFingerprintProofVerifier::VerifyProof(
    const std::string& /*hostname*/,
    const uint16_t /*port*/,
    const std::string& /*server_config*/,
    QuicTransportVersion /*transport_version*/,
    quiche::QuicheStringPiece /*chlo_hash*/,
    const std::vector<std::string>& /*certs*/,
    const std::string& /*cert_sct*/,
    const std::string& /*signature*/,
    const ProofVerifyContext* /*context*/,
    std::string* error_details,
    std::unique_ptr<ProofVerifyDetails>* details,
    std::unique_ptr<ProofVerifierCallback> /*callback*/) {
  *error_details =
      "QUIC crypto certificate verification is not supported in "
      "WebTransportFingerprintProofVerifier";
  QUIC_BUG << *error_details;
  *details = std::make_unique<Details>(Status::kInternalError);
  return QUIC_FAILURE;
}

QuicAsyncStatus WebTransportFingerprintProofVerifier::VerifyCertChain(
    const std::string& /*hostname*/,
    const uint16_t /*port*/,
    const std::vector<std::string>& certs,
    const std::string& /*ocsp_response*/,
    const std::string& /*cert_sct*/,
    const ProofVerifyContext* /*context*/,
    std::string* error_details,
    std::unique_ptr<ProofVerifyDetails>* details,
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
    *error_details = quiche::QuicheStrCat(
        "Certificate expiry exceeds the configured limit of ",
        max_validity_days_, " days");
    return QUIC_FAILURE;
  }

  if (!IsWithinValidityPeriod(*view)) {
    *details = std::make_unique<Details>(Status::kExpired);
    *error_details =
        "Certificate has expired or has validity listed in the future";
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
    quiche::QuicheStringPiece der_certificate) {
  // https://wicg.github.io/web-transport/#verify-a-certificate-fingerprint
  const std::string fingerprint = ComputeSha256Fingerprint(der_certificate);
  for (const CertificateFingerprint& reference : fingerprints_) {
    if (reference.algorithm != CertificateFingerprint::kSha256) {
      QUIC_BUG << "Unexpected non-SHA-256 hash";
      continue;
    }
    if (fingerprint == reference.fingerprint) {
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

}  // namespace quic
