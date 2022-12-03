// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_H_
#define QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"

namespace quic {
namespace test {

// Implementation of ProofSource which delegates to a ProofSourceForTesting, but
// allows for overriding certain functionality. FakeProofSource allows
// intercepting calls to GetProof and ComputeTlsSignature to force them to run
// asynchronously, and allow the caller to see that the call is pending and
// resume the operation at the caller's choosing. FakeProofSource also allows
// the caller to replace the TicketCrypter provided by
// FakeProofSource::GetTicketCrypter.
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
                const QuicSocketAddress& client_address,
                const std::string& hostname, const std::string& server_config,
                QuicTransportVersion transport_version,
                absl::string_view chlo_hash,
                std::unique_ptr<ProofSource::Callback> callback) override;
  quiche::QuicheReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address, const std::string& hostname,
      bool* cert_matched_sni) override;
  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address, const std::string& hostname,
      uint16_t signature_algorithm, absl::string_view in,
      std::unique_ptr<ProofSource::SignatureCallback> callback) override;
  absl::InlinedVector<uint16_t, 8> SupportedTlsSignatureAlgorithms()
      const override;
  TicketCrypter* GetTicketCrypter() override;

  // Sets the TicketCrypter to use. If nullptr, the TicketCrypter from
  // ProofSourceForTesting will be returned instead.
  void SetTicketCrypter(std::unique_ptr<TicketCrypter> ticket_crypter);

  // Get the number of callbacks which are pending
  int NumPendingCallbacks() const;

  // Invoke a pending callback.  The index refers to the position in
  // pending_ops_ of the callback to be completed.
  void InvokePendingCallback(int n);

 private:
  std::unique_ptr<ProofSource> delegate_;
  std::unique_ptr<TicketCrypter> ticket_crypter_;
  bool active_ = false;

  class PendingOp {
   public:
    virtual ~PendingOp();
    virtual void Run() = 0;
  };

  class GetProofOp : public PendingOp {
   public:
    GetProofOp(const QuicSocketAddress& server_addr,
               const QuicSocketAddress& client_address, std::string hostname,
               std::string server_config,
               QuicTransportVersion transport_version, std::string chlo_hash,
               std::unique_ptr<ProofSource::Callback> callback,
               ProofSource* delegate);
    ~GetProofOp() override;

    void Run() override;

   private:
    QuicSocketAddress server_address_;
    QuicSocketAddress client_address_;
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
                       const QuicSocketAddress& client_address,
                       std::string hostname, uint16_t sig_alg,
                       absl::string_view in,
                       std::unique_ptr<ProofSource::SignatureCallback> callback,
                       ProofSource* delegate);
    ~ComputeSignatureOp() override;

    void Run() override;

   private:
    QuicSocketAddress server_address_;
    QuicSocketAddress client_address_;
    std::string hostname_;
    uint16_t sig_alg_;
    std::string in_;
    std::unique_ptr<ProofSource::SignatureCallback> callback_;
    ProofSource* delegate_;
  };

  std::vector<std::unique_ptr<PendingOp>> pending_ops_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_H_
