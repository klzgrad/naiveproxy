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
#include "quic/core/crypto/certificate_view.h"
#include "quic/core/crypto/proof_source.h"
#include "quic/platform/api/quic_containers.h"

namespace quic {

// ProofSourceX509 accepts X.509 certificates with private keys and picks a
// certificate internally based on its SubjectAltName value.
class QUIC_EXPORT_PRIVATE ProofSourceX509 : public ProofSource {
 public:
  // Creates a proof source that uses |default_chain| when no SubjectAltName
  // value matches.  Returns nullptr if |default_chain| is invalid.
  static std::unique_ptr<ProofSourceX509> Create(
      QuicReferenceCountedPointer<Chain> default_chain,
      CertificatePrivateKey default_key);

  // ProofSource implementation.
  void GetProof(const QuicSocketAddress& server_address,
                const QuicSocketAddress& client_address,
                const std::string& hostname,
                const std::string& server_config,
                QuicTransportVersion transport_version,
                absl::string_view chlo_hash,
                std::unique_ptr<Callback> callback) override;
  QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      const std::string& hostname) override;
  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      const std::string& hostname,
      uint16_t signature_algorithm,
      absl::string_view in,
      std::unique_ptr<SignatureCallback> callback) override;
  TicketCrypter* GetTicketCrypter() override;

  // Adds a certificate chain to the verifier.  Returns false if the chain is
  // not valid.  Newer certificates will override older certificates with the
  // same SubjectAltName value.
  ABSL_MUST_USE_RESULT bool AddCertificateChain(
      QuicReferenceCountedPointer<Chain> chain,
      CertificatePrivateKey key);

 private:
  ProofSourceX509() = default;

  struct QUIC_EXPORT_PRIVATE Certificate {
    QuicReferenceCountedPointer<Chain> chain;
    CertificatePrivateKey key;
  };

  // Looks up certficiate for hostname, returns the default if no certificate is
  // found.
  Certificate* GetCertificate(const std::string& hostname) const;

  std::forward_list<Certificate> certificates_;
  Certificate* default_certificate_;
  absl::node_hash_map<std::string, Certificate*> certificate_map_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_PROOF_SOURCE_X509_H_
