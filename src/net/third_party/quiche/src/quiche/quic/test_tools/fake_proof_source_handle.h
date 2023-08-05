// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_HANDLE_H_
#define QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_HANDLE_H_

#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_connection_id.h"

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
    // Similar to FAIL_SYNC, but do not QUICHE_CHECK(!closed_) when invoked.
    FAIL_SYNC_DO_NOT_CHECK_CLOSED,
  };

  // |delegate| must do cert selection and signature synchronously.
  // |dealyed_ssl_config| is the config passed to OnSelectCertificateDone.
  FakeProofSourceHandle(
      ProofSource* delegate, ProofSourceHandleCallback* callback,
      Action select_cert_action, Action compute_signature_action,
      QuicDelayedSSLConfig dealyed_ssl_config = QuicDelayedSSLConfig());

  ~FakeProofSourceHandle() override = default;

  void CloseHandle() override;

  QuicAsyncStatus SelectCertificate(
      const QuicSocketAddress& server_address,
      const QuicSocketAddress& client_address,
      const QuicConnectionId& original_connection_id,
      absl::string_view ssl_capabilities, const std::string& hostname,
      absl::string_view client_hello, const std::string& alpn,
      absl::optional<std::string> alps,
      const std::vector<uint8_t>& quic_transport_params,
      const absl::optional<std::vector<uint8_t>>& early_data_context,
      const QuicSSLConfig& ssl_config) override;

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

  struct SelectCertArgs {
    SelectCertArgs(QuicSocketAddress server_address,
                   QuicSocketAddress client_address,
                   QuicConnectionId original_connection_id,
                   absl::string_view ssl_capabilities, std::string hostname,
                   absl::string_view client_hello, std::string alpn,
                   absl::optional<std::string> alps,
                   std::vector<uint8_t> quic_transport_params,
                   absl::optional<std::vector<uint8_t>> early_data_context,
                   QuicSSLConfig ssl_config)
        : server_address(server_address),
          client_address(client_address),
          original_connection_id(original_connection_id),
          ssl_capabilities(ssl_capabilities),
          hostname(hostname),
          client_hello(client_hello),
          alpn(alpn),
          alps(alps),
          quic_transport_params(quic_transport_params),
          early_data_context(early_data_context),
          ssl_config(ssl_config) {}

    QuicSocketAddress server_address;
    QuicSocketAddress client_address;
    QuicConnectionId original_connection_id;
    std::string ssl_capabilities;
    std::string hostname;
    std::string client_hello;
    std::string alpn;
    absl::optional<std::string> alps;
    std::vector<uint8_t> quic_transport_params;
    absl::optional<std::vector<uint8_t>> early_data_context;
    QuicSSLConfig ssl_config;
  };

  struct ComputeSignatureArgs {
    ComputeSignatureArgs(QuicSocketAddress server_address,
                         QuicSocketAddress client_address, std::string hostname,
                         uint16_t signature_algorithm, absl::string_view in,
                         size_t max_signature_size)
        : server_address(server_address),
          client_address(client_address),
          hostname(hostname),
          signature_algorithm(signature_algorithm),
          in(in),
          max_signature_size(max_signature_size) {}

    QuicSocketAddress server_address;
    QuicSocketAddress client_address;
    std::string hostname;
    uint16_t signature_algorithm;
    std::string in;
    size_t max_signature_size;
  };

  std::vector<SelectCertArgs> all_select_cert_args() const {
    return all_select_cert_args_;
  }

  std::vector<ComputeSignatureArgs> all_compute_signature_args() const {
    return all_compute_signature_args_;
  }

 private:
  class PendingOperation {
   public:
    PendingOperation(ProofSource* delegate, ProofSourceHandleCallback* callback,
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
    SelectCertOperation(ProofSource* delegate,
                        ProofSourceHandleCallback* callback, Action action,
                        SelectCertArgs args,
                        QuicDelayedSSLConfig dealyed_ssl_config);

    ~SelectCertOperation() override = default;

    void Run() override;

   private:
    const SelectCertArgs args_;
    const QuicDelayedSSLConfig dealyed_ssl_config_;
  };

  class ComputeSignatureOperation : public PendingOperation {
   public:
    ComputeSignatureOperation(ProofSource* delegate,
                              ProofSourceHandleCallback* callback,
                              Action action, ComputeSignatureArgs args);

    ~ComputeSignatureOperation() override = default;

    void Run() override;

   private:
    const ComputeSignatureArgs args_;
  };

 private:
  int NumPendingOperations() const;

  bool closed_ = false;
  ProofSource* delegate_;
  ProofSourceHandleCallback* callback_;
  // Action for the next select cert operation.
  Action select_cert_action_ = Action::DELEGATE_SYNC;
  // Action for the next compute signature operation.
  Action compute_signature_action_ = Action::DELEGATE_SYNC;
  const QuicDelayedSSLConfig dealyed_ssl_config_;
  absl::optional<SelectCertOperation> select_cert_op_;
  absl::optional<ComputeSignatureOperation> compute_signature_op_;

  // Save all the select cert and compute signature args for tests to inspect.
  std::vector<SelectCertArgs> all_select_cert_args_;
  std::vector<ComputeSignatureArgs> all_compute_signature_args_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_FAKE_PROOF_SOURCE_HANDLE_H_
