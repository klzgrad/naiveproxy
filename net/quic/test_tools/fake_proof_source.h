// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_H_
#define NET_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_H_

#include <memory>
#include <string>
#include <vector>

#include "net/quic/core/crypto/proof_source.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {
namespace test {

// Implementation of ProofSource which delegates to a ProofSourceForTesting,
// except that when the async GetProof is called, it captures the call and
// allows tests to see that a call is pending, which they can then cause to
// complete at a time of their choosing.
class FakeProofSource : public ProofSource {
 public:
  FakeProofSource();
  ~FakeProofSource() override;

  // Before this object is "active", all calls to GetProof will be delegated
  // immediately.  Once "active", the async ones will be intercepted.  This
  // distinction is necessary to ensure that GetProof can be called without
  // interference during test case setup.
  void Activate();

  // ProofSource interface
  void GetProof(const QuicSocketAddress& server_address,
                const std::string& hostname,
                const std::string& server_config,
                QuicTransportVersion transport_version,
                QuicStringPiece chlo_hash,
                std::unique_ptr<ProofSource::Callback> callback) override;
  QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const std::string& hostname) override;
  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const std::string& hostname,
      uint16_t signature_algorithm,
      QuicStringPiece in,
      std::unique_ptr<ProofSource::SignatureCallback> callback) override;

  // Get the number of callbacks which are pending
  int NumPendingCallbacks() const;

  // Invoke a pending callback.  The index refers to the position in
  // pending_ops_ of the callback to be completed.
  void InvokePendingCallback(int n);

 private:
  std::unique_ptr<ProofSource> delegate_;
  bool active_ = false;

  class PendingOp {
   public:
    virtual ~PendingOp();
    virtual void Run() = 0;
  };

  class GetProofOp : public PendingOp {
   public:
    GetProofOp(const QuicSocketAddress& server_addr,
               std::string hostname,
               std::string server_config,
               QuicTransportVersion transport_version,
               std::string chlo_hash,
               std::unique_ptr<ProofSource::Callback> callback,
               ProofSource* delegate);
    ~GetProofOp() override;

    void Run() override;

   private:
    QuicSocketAddress server_address_;
    std::string hostname_;
    std::string server_config_;
    QuicTransportVersion transport_version_;
    std::string chlo_hash_;
    std::unique_ptr<ProofSource::Callback> callback_;
    ProofSource* delegate_;
  };

  class ComputeSignatureOp : public PendingOp {
   public:
    ComputeSignatureOp(const QuicSocketAddress& server_address,
                       std::string hostname,
                       uint16_t sig_alg,
                       QuicStringPiece in,
                       std::unique_ptr<ProofSource::SignatureCallback> callback,
                       ProofSource* delegate);
    ~ComputeSignatureOp() override;

    void Run() override;

   private:
    QuicSocketAddress server_address_;
    std::string hostname_;
    uint16_t sig_alg_;
    std::string in_;
    std::unique_ptr<ProofSource::SignatureCallback> callback_;
    ProofSource* delegate_;
  };

  std::vector<std::unique_ptr<PendingOp>> pending_ops_;
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_H_
