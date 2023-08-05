// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/fake_proof_source.h"

#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"

namespace quic {
namespace test {

FakeProofSource::FakeProofSource()
    : delegate_(crypto_test_utils::ProofSourceForTesting()) {}

FakeProofSource::~FakeProofSource() {}

FakeProofSource::PendingOp::~PendingOp() = default;

FakeProofSource::GetProofOp::GetProofOp(
    const QuicSocketAddress& server_addr,
    const QuicSocketAddress& client_address, std::string hostname,
    std::string server_config, QuicTransportVersion transport_version,
    std::string chlo_hash, std::unique_ptr<ProofSource::Callback> callback,
    ProofSource* delegate)
    : server_address_(server_addr),
      client_address_(client_address),
      hostname_(std::move(hostname)),
      server_config_(std::move(server_config)),
      transport_version_(transport_version),
      chlo_hash_(std::move(chlo_hash)),
      callback_(std::move(callback)),
      delegate_(delegate) {}

FakeProofSource::GetProofOp::~GetProofOp() = default;

void FakeProofSource::GetProofOp::Run() {
  // Note: relies on the callback being invoked synchronously
  delegate_->GetProof(server_address_, client_address_, hostname_,
                      server_config_, transport_version_, chlo_hash_,
                      std::move(callback_));
}

FakeProofSource::ComputeSignatureOp::ComputeSignatureOp(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address, std::string hostname,
    uint16_t sig_alg, absl::string_view in,
    std::unique_ptr<ProofSource::SignatureCallback> callback,
    ProofSource* delegate)
    : server_address_(server_address),
      client_address_(client_address),
      hostname_(std::move(hostname)),
      sig_alg_(sig_alg),
      in_(in),
      callback_(std::move(callback)),
      delegate_(delegate) {}

FakeProofSource::ComputeSignatureOp::~ComputeSignatureOp() = default;

void FakeProofSource::ComputeSignatureOp::Run() {
  delegate_->ComputeTlsSignature(server_address_, client_address_, hostname_,
                                 sig_alg_, in_, std::move(callback_));
}

void FakeProofSource::Activate() { active_ = true; }

void FakeProofSource::GetProof(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address, const std::string& hostname,
    const std::string& server_config, QuicTransportVersion transport_version,
    absl::string_view chlo_hash,
    std::unique_ptr<ProofSource::Callback> callback) {
  if (!active_) {
    delegate_->GetProof(server_address, client_address, hostname, server_config,
                        transport_version, chlo_hash, std::move(callback));
    return;
  }

  pending_ops_.push_back(std::make_unique<GetProofOp>(
      server_address, client_address, hostname, server_config,
      transport_version, std::string(chlo_hash), std::move(callback),
      delegate_.get()));
}

quiche::QuicheReferenceCountedPointer<ProofSource::Chain>
FakeProofSource::GetCertChain(const QuicSocketAddress& server_address,
                              const QuicSocketAddress& client_address,
                              const std::string& hostname,
                              bool* cert_matched_sni) {
  return delegate_->GetCertChain(server_address, client_address, hostname,
                                 cert_matched_sni);
}

void FakeProofSource::ComputeTlsSignature(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address, const std::string& hostname,
    uint16_t signature_algorithm, absl::string_view in,
    std::unique_ptr<ProofSource::SignatureCallback> callback) {
  QUIC_LOG(INFO) << "FakeProofSource::ComputeTlsSignature";
  if (!active_) {
    QUIC_LOG(INFO) << "Not active - directly calling delegate";
    delegate_->ComputeTlsSignature(server_address, client_address, hostname,
                                   signature_algorithm, in,
                                   std::move(callback));
    return;
  }

  QUIC_LOG(INFO) << "Adding pending op";
  pending_ops_.push_back(std::make_unique<ComputeSignatureOp>(
      server_address, client_address, hostname, signature_algorithm, in,
      std::move(callback), delegate_.get()));
}

absl::InlinedVector<uint16_t, 8>
FakeProofSource::SupportedTlsSignatureAlgorithms() const {
  return delegate_->SupportedTlsSignatureAlgorithms();
}

ProofSource::TicketCrypter* FakeProofSource::GetTicketCrypter() {
  if (ticket_crypter_) {
    return ticket_crypter_.get();
  }
  return delegate_->GetTicketCrypter();
}

void FakeProofSource::SetTicketCrypter(
    std::unique_ptr<TicketCrypter> ticket_crypter) {
  ticket_crypter_ = std::move(ticket_crypter);
}

int FakeProofSource::NumPendingCallbacks() const { return pending_ops_.size(); }

void FakeProofSource::InvokePendingCallback(int n) {
  QUICHE_CHECK(NumPendingCallbacks() > n);

  pending_ops_[n]->Run();

  auto it = pending_ops_.begin() + n;
  pending_ops_.erase(it);
}

}  // namespace test
}  // namespace quic
