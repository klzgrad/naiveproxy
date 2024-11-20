// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/fake_proof_source_handle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_reference_counted.h"

namespace quic {
namespace test {
namespace {

struct ComputeSignatureResult {
  bool ok;
  std::string signature;
  std::unique_ptr<ProofSource::Details> details;
};

class ResultSavingSignatureCallback : public ProofSource::SignatureCallback {
 public:
  explicit ResultSavingSignatureCallback(
      std::optional<ComputeSignatureResult>* result)
      : result_(result) {
    QUICHE_DCHECK(!result_->has_value());
  }
  void Run(bool ok, std::string signature,
           std::unique_ptr<ProofSource::Details> details) override {
    result_->emplace(
        ComputeSignatureResult{ok, std::move(signature), std::move(details)});
  }

 private:
  std::optional<ComputeSignatureResult>* result_;
};

ComputeSignatureResult ComputeSignatureNow(
    ProofSource* delegate, const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address, const std::string& hostname,
    uint16_t signature_algorithm, absl::string_view in) {
  std::optional<ComputeSignatureResult> result;
  delegate->ComputeTlsSignature(
      server_address, client_address, hostname, signature_algorithm, in,
      std::make_unique<ResultSavingSignatureCallback>(&result));
  QUICHE_CHECK(result.has_value())
      << "delegate->ComputeTlsSignature must computes a "
         "signature immediately";
  return std::move(result.value());
}
}  // namespace

FakeProofSourceHandle::FakeProofSourceHandle(
    ProofSource* delegate, ProofSourceHandleCallback* callback,
    Action select_cert_action, Action compute_signature_action,
    QuicDelayedSSLConfig delayed_ssl_config)
    : delegate_(delegate),
      callback_(callback),
      select_cert_action_(select_cert_action),
      compute_signature_action_(compute_signature_action),
      delayed_ssl_config_(delayed_ssl_config) {}

void FakeProofSourceHandle::CloseHandle() {
  select_cert_op_.reset();
  compute_signature_op_.reset();
  closed_ = true;
}

QuicAsyncStatus FakeProofSourceHandle::SelectCertificate(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address,
    const QuicConnectionId& original_connection_id,
    absl::string_view ssl_capabilities, const std::string& hostname,
    absl::string_view client_hello, const std::string& alpn,
    std::optional<std::string> alps,
    const std::vector<uint8_t>& quic_transport_params,
    const std::optional<std::vector<uint8_t>>& early_data_context,
    const QuicSSLConfig& ssl_config) {
  if (select_cert_action_ != Action::FAIL_SYNC_DO_NOT_CHECK_CLOSED) {
    QUICHE_CHECK(!closed_);
  }
  all_select_cert_args_.push_back(
      SelectCertArgs(server_address, client_address, original_connection_id,
                     ssl_capabilities, hostname, client_hello, alpn, alps,
                     quic_transport_params, early_data_context, ssl_config));

  if (select_cert_action_ == Action::DELEGATE_ASYNC ||
      select_cert_action_ == Action::FAIL_ASYNC) {
    select_cert_op_.emplace(delegate_, callback_, select_cert_action_,
                            all_select_cert_args_.back(), delayed_ssl_config_);
    return QUIC_PENDING;
  } else if (select_cert_action_ == Action::FAIL_SYNC ||
             select_cert_action_ == Action::FAIL_SYNC_DO_NOT_CHECK_CLOSED) {
    callback()->OnSelectCertificateDone(
        /*ok=*/false,
        /*is_sync=*/true,
        ProofSourceHandleCallback::LocalSSLConfig{nullptr, delayed_ssl_config_},
        /*ticket_encryption_key=*/absl::string_view(),
        /*cert_matched_sni=*/false);
    return QUIC_FAILURE;
  }

  QUICHE_DCHECK(select_cert_action_ == Action::DELEGATE_SYNC);
  bool cert_matched_sni;
  quiche::QuicheReferenceCountedPointer<ProofSource::Chain> chain =
      delegate_->GetCertChain(server_address, client_address, hostname,
                              &cert_matched_sni);

  bool ok = chain && !chain->certs.empty();
  callback_->OnSelectCertificateDone(
      ok, /*is_sync=*/true,
      ProofSourceHandleCallback::LocalSSLConfig{chain.get(),
                                                delayed_ssl_config_},
      /*ticket_encryption_key=*/absl::string_view(),
      /*cert_matched_sni=*/cert_matched_sni);
  return ok ? QUIC_SUCCESS : QUIC_FAILURE;
}

QuicAsyncStatus FakeProofSourceHandle::ComputeSignature(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address, const std::string& hostname,
    uint16_t signature_algorithm, absl::string_view in,
    size_t max_signature_size) {
  if (compute_signature_action_ != Action::FAIL_SYNC_DO_NOT_CHECK_CLOSED) {
    QUICHE_CHECK(!closed_);
  }
  all_compute_signature_args_.push_back(
      ComputeSignatureArgs(server_address, client_address, hostname,
                           signature_algorithm, in, max_signature_size));

  if (compute_signature_action_ == Action::DELEGATE_ASYNC ||
      compute_signature_action_ == Action::FAIL_ASYNC) {
    compute_signature_op_.emplace(delegate_, callback_,
                                  compute_signature_action_,
                                  all_compute_signature_args_.back());
    return QUIC_PENDING;
  } else if (compute_signature_action_ == Action::FAIL_SYNC ||
             compute_signature_action_ ==
                 Action::FAIL_SYNC_DO_NOT_CHECK_CLOSED) {
    callback()->OnComputeSignatureDone(/*ok=*/false, /*is_sync=*/true,
                                       /*signature=*/"", /*details=*/nullptr);
    return QUIC_FAILURE;
  }

  QUICHE_DCHECK(compute_signature_action_ == Action::DELEGATE_SYNC);
  ComputeSignatureResult result =
      ComputeSignatureNow(delegate_, server_address, client_address, hostname,
                          signature_algorithm, in);
  callback_->OnComputeSignatureDone(
      result.ok, /*is_sync=*/true, result.signature, std::move(result.details));
  return result.ok ? QUIC_SUCCESS : QUIC_FAILURE;
}

ProofSourceHandleCallback* FakeProofSourceHandle::callback() {
  return callback_;
}

bool FakeProofSourceHandle::HasPendingOperation() const {
  int num_pending_operations = NumPendingOperations();
  return num_pending_operations > 0;
}

void FakeProofSourceHandle::CompletePendingOperation() {
  QUICHE_DCHECK_LE(NumPendingOperations(), 1);

  if (select_cert_op_.has_value()) {
    select_cert_op_->Run();
    select_cert_op_.reset();
  } else if (compute_signature_op_.has_value()) {
    compute_signature_op_->Run();
    compute_signature_op_.reset();
  }
}

int FakeProofSourceHandle::NumPendingOperations() const {
  return static_cast<int>(select_cert_op_.has_value()) +
         static_cast<int>(compute_signature_op_.has_value());
}

FakeProofSourceHandle::SelectCertOperation::SelectCertOperation(
    ProofSource* delegate, ProofSourceHandleCallback* callback, Action action,
    SelectCertArgs args, QuicDelayedSSLConfig delayed_ssl_config)
    : PendingOperation(delegate, callback, action),
      args_(std::move(args)),
      delayed_ssl_config_(delayed_ssl_config) {}

void FakeProofSourceHandle::SelectCertOperation::Run() {
  if (action_ == Action::FAIL_ASYNC) {
    callback_->OnSelectCertificateDone(
        /*ok=*/false,
        /*is_sync=*/false,
        ProofSourceHandleCallback::LocalSSLConfig{nullptr, delayed_ssl_config_},
        /*ticket_encryption_key=*/absl::string_view(),
        /*cert_matched_sni=*/false);
  } else if (action_ == Action::DELEGATE_ASYNC) {
    bool cert_matched_sni;
    quiche::QuicheReferenceCountedPointer<ProofSource::Chain> chain =
        delegate_->GetCertChain(args_.server_address, args_.client_address,
                                args_.hostname, &cert_matched_sni);
    bool ok = chain && !chain->certs.empty();
    callback_->OnSelectCertificateDone(
        ok, /*is_sync=*/false,
        ProofSourceHandleCallback::LocalSSLConfig{chain.get(),
                                                  delayed_ssl_config_},
        /*ticket_encryption_key=*/absl::string_view(),
        /*cert_matched_sni=*/cert_matched_sni);
  } else {
    QUIC_BUG(quic_bug_10139_1)
        << "Unexpected action: " << static_cast<int>(action_);
  }
}

FakeProofSourceHandle::ComputeSignatureOperation::ComputeSignatureOperation(
    ProofSource* delegate, ProofSourceHandleCallback* callback, Action action,
    ComputeSignatureArgs args)
    : PendingOperation(delegate, callback, action), args_(std::move(args)) {}

void FakeProofSourceHandle::ComputeSignatureOperation::Run() {
  if (action_ == Action::FAIL_ASYNC) {
    callback_->OnComputeSignatureDone(
        /*ok=*/false, /*is_sync=*/false,
        /*signature=*/"", /*details=*/nullptr);
  } else if (action_ == Action::DELEGATE_ASYNC) {
    ComputeSignatureResult result = ComputeSignatureNow(
        delegate_, args_.server_address, args_.client_address, args_.hostname,
        args_.signature_algorithm, args_.in);
    callback_->OnComputeSignatureDone(result.ok, /*is_sync=*/false,
                                      result.signature,
                                      std::move(result.details));
  } else {
    QUIC_BUG(quic_bug_10139_2)
        << "Unexpected action: " << static_cast<int>(action_);
  }
}

}  // namespace test
}  // namespace quic
