// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/proof_source.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "openssl/base.h"
#include "openssl/ex_data.h"
#include "openssl/pool.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_reference_counted.h"

namespace quic {

namespace {

void CredentialExDataFree(void*, void* ptr, CRYPTO_EX_DATA*, int,
                          long,  // NOLINT
                          void*) {
  delete static_cast<CredentialExData*>(ptr);
}

int GetCredentialExDataIndex() {
  static const int index = [] {
    int ret = SSL_CREDENTIAL_get_ex_new_index(0, nullptr, nullptr, nullptr,
                                              CredentialExDataFree);
    if (ret < 0) {
      QUIC_BUG(quic_credential_ex_data_index_failure)
          << "Failed to get SSL credential ex data index. "
             "(Get|Set)CredentialExData will not work.";
    }
    return ret;
  }();
  return index;
}

}  // namespace

CryptoBuffers::~CryptoBuffers() {
  for (size_t i = 0; i < value.size(); i++) {
    CRYPTO_BUFFER_free(value[i]);
  }
}

void SetCredentialExData(SSL_CREDENTIAL& credential,
                         std::unique_ptr<CredentialExData> exdata) {
  int index = GetCredentialExDataIndex();
  if (index < 0 || exdata == nullptr) {
    return;
  }
  if (SSL_CREDENTIAL_set_ex_data(&credential, index, exdata.get())) {
    exdata.release();  // Ownership transferred.
  } else {
    QUICHE_LOG_FIRST_N(ERROR, 1) << "SetCredentialExData failed.";
  }
}

const CredentialExData* GetCredentialExData(const SSL_CREDENTIAL& credential) {
  int index = GetCredentialExDataIndex();
  if (index < 0) {
    return nullptr;
  }
  return static_cast<const CredentialExData*>(
      SSL_CREDENTIAL_get_ex_data(&credential, index));
}

ProofSource::Chain::Chain(const std::vector<std::string>& certs,
                          const std::string& trust_anchor_id)
    : certs(certs), trust_anchor_id(trust_anchor_id) {}

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

ProofSource::CertChainsResult ProofSource::GetCertChains(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address, const std::string& hostname) {
  bool cert_matched_sni;
  quiche::QuicheReferenceCountedPointer<Chain> chain =
      GetCertChain(server_address, client_address, hostname, &cert_matched_sni);
  return chain == nullptr ? CertChainsResult{}
                          : CertChainsResult{
                                .chains_match_sni = cert_matched_sni,
                                .chains = {chain},
                            };
}

}  // namespace quic
