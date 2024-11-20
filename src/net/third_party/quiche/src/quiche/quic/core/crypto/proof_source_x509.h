// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_X509_H_
#define QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_X509_H_

#include <forward_list>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/quic_crypto_proof.h"

namespace quic {

// ProofSourceX509 accepts X.509 certificates with private keys and picks a
// certificate internally based on its SubjectAltName value.
class QUICHE_EXPORT ProofSourceX509 : public ProofSource {
 public:
  // Creates a proof source that uses |default_chain| when no SubjectAltName
  // value matches.  Returns nullptr if |default_chain| is invalid.
  static std::unique_ptr<ProofSourceX509> Create(
      quiche::QuicheReferenceCountedPointer<Chain> default_chain,
      CertificatePrivateKey default_key);

  // ProofSource implementation.
  void GetProof(const QuicSocketAddress& server_address,
                const QuicSocketAddress& client_address,
                const std::string& hostname, const std::string& server_config,
                QuicTransportVersion transport_version,
                absl::string_view chlo_hash,
                std::unique_ptr<Callback> callback) override;
  quiche::QuicheReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address, const std::string& hostname,
      bool* cert_matched_sni) override;
  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address, const std::string& hostname,
      uint16_t signature_algorithm, absl::string_view in,
      std::unique_ptr<SignatureCallback> callback) override;
  QuicSignatureAlgorithmVector SupportedTlsSignatureAlgorithms() const override;
  TicketCrypter* GetTicketCrypter() override;

  // Adds a certificate chain to the verifier.  Returns false if the chain is
  // not valid.  Newer certificates will override older certificates with the
  // same SubjectAltName value.
  ABSL_MUST_USE_RESULT bool AddCertificateChain(
      quiche::QuicheReferenceCountedPointer<Chain> chain,
      CertificatePrivateKey key);

 protected:
  ProofSourceX509(quiche::QuicheReferenceCountedPointer<Chain> default_chain,
                  CertificatePrivateKey default_key);
  bool valid() const { return default_certificate_ != nullptr; }

  // Gives an opportunity for the subclass proof source to provide SCTs for a
  // given hostname.
  virtual void MaybeAddSctsForHostname(absl::string_view /*hostname*/,
                                       std::string& /*leaf_cert_scts*/) {}

 private:
  struct QUICHE_EXPORT Certificate {
    quiche::QuicheReferenceCountedPointer<Chain> chain;
    CertificatePrivateKey key;
  };

  // Looks up certficiate for hostname, returns the default if no certificate is
  // found.
  Certificate* GetCertificate(const std::string& hostname,
                              bool* cert_matched_sni) const;

  std::forward_list<Certificate> certificates_;
  Certificate* default_certificate_ = nullptr;
  absl::node_hash_map<std::string, Certificate*> certificate_map_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_X509_H_
