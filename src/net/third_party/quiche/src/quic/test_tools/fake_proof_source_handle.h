// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_HANDLE_H_
#define QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_HANDLE_H_

#include "quic/core/crypto/proof_source.h"

namespace quic {
namespace test {

// FakeProofSourceHandle allows its behavior to be scripted for testing.
class FakeProofSourceHandle : public ProofSourceHandle {
 public:
  // What would an operation return when it is called.
  enum class Action {
    // Delegate the operation to |delegate_| immediately.
    DELEGATE_SYNC = 0,
    // Handle the operation asynchronously. Delegate the operation to
    // |delegate_| when the caller calls CompletePendingOperation().
    DELEGATE_ASYNC,
    // Fail the operation immediately.
    FAIL_SYNC,
    // Handle the operation asynchronously. Fail the operation when the caller
    // calls CompletePendingOperation().
    FAIL_ASYNC,
  };

  // |delegate| must do cert selection and signature synchronously.
  FakeProofSourceHandle(ProofSource* delegate,
                        ProofSourceHandleCallback* callback,
                        Action select_cert_action,
                        Action compute_signature_action);

  ~FakeProofSourceHandle() override = default;

  void CancelPendingOperation() override;

  QuicAsyncStatus SelectCertificate(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      const std::string& hostname,
      absl::string_view client_hello,
      const std::string& alpn,
      const std::vector<uint8_t>& quic_transport_params,
      const absl::optional<std::vector<uint8_t>>& early_data_context) override;

  QuicAsyncStatus ComputeSignature(const QuicSocketAddress& server_address,
                                   const QuicSocketAddress& client_address,
                                   const std::string& hostname,
                                   uint16_t signature_algorithm,
                                   absl::string_view in,
                                   size_t max_signature_size) override;

  ProofSourceHandleCallback* callback() override;

  // Whether there's a pending operation in |this|.
  bool HasPendingOperation() const;
  void CompletePendingOperation();

 private:
  class PendingOperation {
   public:
    PendingOperation(ProofSource* delegate,
                     ProofSourceHandleCallback* callback,
                     Action action)
        : delegate_(delegate), callback_(callback), action_(action) {}
    virtual ~PendingOperation() = default;
    virtual void Run() = 0;

   protected:
    ProofSource* delegate_;
    ProofSourceHandleCallback* callback_;
    Action action_;
  };

  class SelectCertOperation : public PendingOperation {
   public:
    SelectCertOperation(
        ProofSource* delegate,
        ProofSourceHandleCallback* callback,
        Action action,
        const QuicSocketAddress& server_address,
        const QuicSocketAddress& client_address,
        const std::string& hostname,
        absl::string_view client_hello,
        const std::string& alpn,
        const std::vector<uint8_t>& quic_transport_params,
        const absl::optional<std::vector<uint8_t>>& early_data_context);

    ~SelectCertOperation() override = default;

    void Run() override;

   private:
    QuicSocketAddress server_address_;
    QuicSocketAddress client_address_;
    std::string hostname_;
    std::string client_hello_;
    std::string alpn_;
    std::vector<uint8_t> quic_transport_params_;
    absl::optional<std::vector<uint8_t>> early_data_context_;
  };

  class ComputeSignatureOperation : public PendingOperation {
   public:
    ComputeSignatureOperation(ProofSource* delegate,
                              ProofSourceHandleCallback* callback,
                              Action action,
                              const QuicSocketAddress& server_address,
                              const QuicSocketAddress& client_address,
                              const std::string& hostname,
                              uint16_t signature_algorithm,
                              absl::string_view in,
                              size_t max_signature_size);

    ~ComputeSignatureOperation() override = default;

    void Run() override;

   private:
    QuicSocketAddress server_address_;
    QuicSocketAddress client_address_;
    std::string hostname_;
    uint16_t signature_algorithm_;
    std::string in_;
    size_t max_signature_size_;
  };

 private:
  int NumPendingOperations() const;

  ProofSource* delegate_;
  ProofSourceHandleCallback* callback_;
  // Action for the next select cert operation.
  Action select_cert_action_ = Action::DELEGATE_SYNC;
  // Action for the next compute signature operation.
  Action compute_signature_action_ = Action::DELEGATE_SYNC;
  absl::optional<SelectCertOperation> select_cert_op_;
  absl::optional<ComputeSignatureOperation> compute_signature_op_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_HANDLE_H_
