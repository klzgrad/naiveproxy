// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/proof_source_x509.h"

#include <memory>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/quiche_endian.h"

namespace quic {

ProofSourceX509::ProofSourceX509(
    quiche::QuicheReferenceCountedPointer<Chain> default_chain,
    CertificatePrivateKey default_key) {
  if (!AddCertificateChain(default_chain, std::move(default_key))) {
    return;
  }
  default_certificate_ = &certificates_.front();
}

std::unique_ptr<ProofSourceX509> ProofSourceX509::Create(
    quiche::QuicheReferenceCountedPointer<Chain> default_chain,
    CertificatePrivateKey default_key) {
  std::unique_ptr<ProofSourceX509> result(
      new ProofSourceX509(default_chain, std::move(default_key)));
  if (!result->valid()) {
    return nullptr;
  }
  return result;
}

void ProofSourceX509::GetProof(
    const QuicSocketAddress& /*server_address*/,
    const QuicSocketAddress& /*client_address*/, const std::string& hostname,
    const std::string& server_config,
    QuicTransportVersion /*transport_version*/, absl::string_view chlo_hash,
    std::unique_ptr<ProofSource::Callback> callback) {
  QuicCryptoProof proof;

  if (!valid()) {
    QUIC_BUG(ProofSourceX509::GetProof called in invalid state)
        << "ProofSourceX509::GetProof called while the object is not valid";
    callback->Run(/*ok=*/false, nullptr, proof, nullptr);
    return;
  }

  absl::optional<std::string> payload =
      CryptoUtils::GenerateProofPayloadToBeSigned(chlo_hash, server_config);
  if (!payload.has_value()) {
    callback->Run(/*ok=*/false, nullptr, proof, nullptr);
    return;
  }

  Certificate* certificate = GetCertificate(hostname, &proof.cert_matched_sni);
  proof.signature =
      certificate->key.Sign(*payload, SSL_SIGN_RSA_PSS_RSAE_SHA256);
  MaybeAddSctsForHostname(hostname, proof.leaf_cert_scts);
  callback->Run(/*ok=*/!proof.signature.empty(), certificate->chain, proof,
                nullptr);
}

quiche::QuicheReferenceCountedPointer<ProofSource::Chain>
ProofSourceX509::GetCertChain(const QuicSocketAddress& /*server_address*/,
                              const QuicSocketAddress& /*client_address*/,
                              const std::string& hostname,
                              bool* cert_matched_sni) {
  if (!valid()) {
    QUIC_BUG(ProofSourceX509::GetCertChain called in invalid state)
        << "ProofSourceX509::GetCertChain called while the object is not "
           "valid";
    return nullptr;
  }

  return GetCertificate(hostname, cert_matched_sni)->chain;
}

void ProofSourceX509::ComputeTlsSignature(
    const QuicSocketAddress& /*server_address*/,
    const QuicSocketAddress& /*client_address*/, const std::string& hostname,
    uint16_t signature_algorithm, absl::string_view in,
    std::unique_ptr<ProofSource::SignatureCallback> callback) {
  if (!valid()) {
    QUIC_BUG(ProofSourceX509::ComputeTlsSignature called in invalid state)
        << "ProofSourceX509::ComputeTlsSignature called while the object is "
           "not valid";
    callback->Run(/*ok=*/false, "", nullptr);
    return;
  }

  bool cert_matched_sni;
  std::string signature = GetCertificate(hostname, &cert_matched_sni)
                              ->key.Sign(in, signature_algorithm);
  callback->Run(/*ok=*/!signature.empty(), signature, nullptr);
}

QuicSignatureAlgorithmVector ProofSourceX509::SupportedTlsSignatureAlgorithms()
    const {
  return SupportedSignatureAlgorithmsForQuic();
}

ProofSource::TicketCrypter* ProofSourceX509::GetTicketCrypter() {
  return nullptr;
}

bool ProofSourceX509::AddCertificateChain(
    quiche::QuicheReferenceCountedPointer<Chain> chain,
    CertificatePrivateKey key) {
  if (chain->certs.empty()) {
    QUIC_BUG(quic_bug_10644_1) << "Empty certificate chain supplied.";
    return false;
  }

  std::unique_ptr<CertificateView> leaf =
      CertificateView::ParseSingleCertificate(chain->certs[0]);
  if (leaf == nullptr) {
    QUIC_BUG(quic_bug_10644_2)
        << "Unable to parse X.509 leaf certificate in the supplied chain.";
    return false;
  }
  if (!key.MatchesPublicKey(*leaf)) {
    QUIC_BUG(quic_bug_10644_3)
        << "Private key does not match the leaf certificate.";
    return false;
  }

  certificates_.push_front(Certificate{
      chain,
      std::move(key),
  });
  Certificate* certificate = &certificates_.front();

  for (absl::string_view host : leaf->subject_alt_name_domains()) {
    certificate_map_[std::string(host)] = certificate;
  }
  return true;
}

ProofSourceX509::Certificate* ProofSourceX509::GetCertificate(
    const std::string& hostname, bool* cert_matched_sni) const {
  QUICHE_DCHECK(valid());
  auto it = certificate_map_.find(hostname);
  if (it != certificate_map_.end()) {
    *cert_matched_sni = true;
    return it->second;
  }
  auto dot_pos = hostname.find('.');
  if (dot_pos != std::string::npos) {
    std::string wildcard = absl::StrCat("*", hostname.substr(dot_pos));
    it = certificate_map_.find(wildcard);
    if (it != certificate_map_.end()) {
      *cert_matched_sni = true;
      return it->second;
    }
  }
  *cert_matched_sni = false;
  return default_certificate_;
}

}  // namespace quic
