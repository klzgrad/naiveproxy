// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/fake_proof_source.h"

#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {

FakeProofSource::FakeProofSource()
    : delegate_(crypto_test_utils::ProofSourceForTesting()) {}

FakeProofSource::~FakeProofSource() {}

FakeProofSource::PendingOp::~PendingOp() = default;

FakeProofSource::GetProofOp::GetProofOp(
    const QuicSocketAddress& server_addr,
    std::string hostname,
    std::string server_config,
    QuicTransportVersion transport_version,
    std::string chlo_hash,
    std::unique_ptr<ProofSource::Callback> callback,
    ProofSource* delegate)
    : server_address_(server_addr),
      hostname_(std::move(hostname)),
      server_config_(std::move(server_config)),
      transport_version_(transport_version),
      chlo_hash_(std::move(chlo_hash)),
      callback_(std::move(callback)),
      delegate_(delegate) {}

FakeProofSource::GetProofOp::~GetProofOp() = default;

void FakeProofSource::GetProofOp::Run() {
  // Note: relies on the callback being invoked synchronously
  delegate_->GetProof(server_address_, hostname_, server_config_,
                      transport_version_, chlo_hash_, std::move(callback_));
}

FakeProofSource::ComputeSignatureOp::ComputeSignatureOp(
    const QuicSocketAddress& server_address,
    std::string hostname,
    uint16_t sig_alg,
    quiche::QuicheStringPiece in,
    std::unique_ptr<ProofSource::SignatureCallback> callback,
    ProofSource* delegate)
    : server_address_(server_address),
      hostname_(std::move(hostname)),
      sig_alg_(sig_alg),
      in_(in),
      callback_(std::move(callback)),
      delegate_(delegate) {}

FakeProofSource::ComputeSignatureOp::~ComputeSignatureOp() = default;

void FakeProofSource::ComputeSignatureOp::Run() {
  delegate_->ComputeTlsSignature(server_address_, hostname_, sig_alg_, in_,
                                 std::move(callback_));
}

void FakeProofSource::Activate() {
  active_ = true;
}

void FakeProofSource::GetProof(
    const QuicSocketAddress& server_address,
    const std::string& hostname,
    const std::string& server_config,
    QuicTransportVersion transport_version,
    quiche::QuicheStringPiece chlo_hash,
    std::unique_ptr<ProofSource::Callback> callback) {
  if (!active_) {
    delegate_->GetProof(server_address, hostname, server_config,
                        transport_version, chlo_hash, std::move(callback));
    return;
  }

  pending_ops_.push_back(std::make_unique<GetProofOp>(
      server_address, hostname, server_config, transport_version,
      std::string(chlo_hash), std::move(callback), delegate_.get()));
}

QuicReferenceCountedPointer<ProofSource::Chain> FakeProofSource::GetCertChain(
    const QuicSocketAddress& server_address,
    const std::string& hostname) {
  return delegate_->GetCertChain(server_address, hostname);
}

void FakeProofSource::ComputeTlsSignature(
    const QuicSocketAddress& server_address,
    const std::string& hostname,
    uint16_t signature_algorithm,
    quiche::QuicheStringPiece in,
    std::unique_ptr<ProofSource::SignatureCallback> callback) {
  QUIC_LOG(INFO) << "FakeProofSource::ComputeTlsSignature";
  if (!active_) {
    QUIC_LOG(INFO) << "Not active - directly calling delegate";
    delegate_->ComputeTlsSignature(
        server_address, hostname, signature_algorithm, in, std::move(callback));
    return;
  }

  QUIC_LOG(INFO) << "Adding pending op";
  pending_ops_.push_back(std::make_unique<ComputeSignatureOp>(
      server_address, hostname, signature_algorithm, in, std::move(callback),
      delegate_.get()));
}

int FakeProofSource::NumPendingCallbacks() const {
  return pending_ops_.size();
}

void FakeProofSource::InvokePendingCallback(int n) {
  CHECK(NumPendingCallbacks() > n);

  pending_ops_[n]->Run();

  auto it = pending_ops_.begin() + n;
  pending_ops_.erase(it);
}

}  // namespace test
}  // namespace quic
