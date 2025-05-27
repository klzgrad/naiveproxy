// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/proof_source.h"

#include <memory>
#include <string>
#include <vector>

#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

CryptoBuffers::~CryptoBuffers() {
  for (size_t i = 0; i < value.size(); i++) {
    CRYPTO_BUFFER_free(value[i]);
  }
}

ProofSource::Chain::Chain(const std::vector<std::string>& certs)
    : certs(certs) {}

ProofSource::Chain::~Chain() {}

CryptoBuffers ProofSource::Chain::ToCryptoBuffers() const {
  CryptoBuffers crypto_buffers;
  crypto_buffers.value.reserve(certs.size());
  for (size_t i = 0; i < certs.size(); i++) {
    crypto_buffers.value.push_back(
        CRYPTO_BUFFER_new(reinterpret_cast<const uint8_t*>(certs[i].data()),
                          certs[i].length(), nullptr));
  }
  return crypto_buffers;
}

bool ValidateCertAndKey(
    const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
    const CertificatePrivateKey& key) {
  if (chain.get() == nullptr || chain->certs.empty()) {
    QUIC_BUG(quic_proof_source_empty_chain) << "Certificate chain is empty";
    return false;
  }

  std::unique_ptr<CertificateView> leaf =
      CertificateView::ParseSingleCertificate(chain->certs[0]);
  if (leaf == nullptr) {
    QUIC_BUG(quic_proof_source_unparsable_leaf_cert)
        << "Unabled to parse leaf certificate";
    return false;
  }

  if (!key.MatchesPublicKey(*leaf)) {
    QUIC_BUG(quic_proof_source_key_mismatch)
        << "Private key does not match the leaf certificate";
    return false;
  }
  return true;
}

void ProofSource::OnNewSslCtx(SSL_CTX*) {}

}  // namespace quic
