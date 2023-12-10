// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/tls_server_handshaker.h"

#include <memory>
#include <string>

#include "absl/base/macros.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/pool.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/crypto/transport_parameters.h"
#include "quiche/quic/core/http/http_encoder.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_hostname_utils.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_server_stats.h"

#define RECORD_LATENCY_IN_US(stat_name, latency, comment)                   \
  do {                                                                      \
    const int64_t latency_in_us = (latency).ToMicroseconds();               \
    QUIC_DVLOG(1) << "Recording " stat_name ": " << latency_in_us;          \
    QUIC_SERVER_HISTOGRAM_COUNTS(stat_name, latency_in_us, 1, 10000000, 50, \
                                 comment);                                  \
  } while (0)

namespace quic {

namespace {

// Default port for HTTP/3.
uint16_t kDefaultPort = 443;

}  // namespace

TlsServerHandshaker::DefaultProofSourceHandle::DefaultProofSourceHandle(
    TlsServerHandshaker* handshaker, ProofSource* proof_source)
    : handshaker_(handshaker), proof_source_(proof_source) {}

TlsServerHandshaker::DefaultProofSourceHandle::~DefaultProofSourceHandle() {
  CloseHandle();
}

void TlsServerHandshaker::DefaultProofSourceHandle::CloseHandle() {
  QUIC_DVLOG(1) << "CloseHandle. is_signature_pending="
                << (signature_callback_ != nullptr);
  if (signature_callback_) {
    signature_callback_->Cancel();
    signature_callback_ = nullptr;
  }
}

QuicAsyncStatus
TlsServerHandshaker::DefaultProofSourceHandle::SelectCertificate(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address,
    const QuicConnectionId& /*original_connection_id*/,
    absl::string_view /*ssl_capabilities*/, const std::string& hostname,
    absl::string_view /*client_hello*/, const std::string& /*alpn*/,
    absl::optional<std::string> /*alps*/,
    const std::vector<uint8_t>& /*quic_transport_params*/,
    const absl::optional<std::vector<uint8_t>>& /*early_data_context*/,
    const QuicSSLConfig& /*ssl_config*/) {
  if (!handshaker_ || !proof_source_) {
    QUIC_BUG(quic_bug_10341_1)
        << "SelectCertificate called on a detached handle";
    return QUIC_FAILURE;
  }

  bool cert_matched_sni;
  quiche::QuicheReferenceCountedPointer<ProofSource::Chain> chain =
      proof_source_->GetCertChain(server_address, client_address, hostname,
                                  &cert_matched_sni);

  handshaker_->OnSelectCertificateDone(
      /*ok=*/true, /*is_sync=*/true, chain.get(),
      /*handshake_hints=*/absl::string_view(),
      /*ticket_encryption_key=*/absl::string_view(), cert_matched_sni,
      QuicDelayedSSLConfig());
  if (!handshaker_->select_cert_status().has_value()) {
    QUIC_BUG(quic_bug_12423_1)
        << "select_cert_status() has no value after a synchronous select cert";
    // Return success to continue the handshake.
    return QUIC_SUCCESS;
  }
  return *handshaker_->select_cert_status();
}

QuicAsyncStatus TlsServerHandshaker::DefaultProofSourceHandle::ComputeSignature(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address, const std::string& hostname,
    uint16_t signature_algorithm, absl::string_view in,
    size_t max_signature_size) {
  if (!handshaker_ || !proof_source_) {
    QUIC_BUG(quic_bug_10341_2)
        << "ComputeSignature called on a detached handle";
    return QUIC_FAILURE;
  }

  if (signature_callback_) {
    QUIC_BUG(quic_bug_10341_3) << "ComputeSignature called while pending";
    return QUIC_FAILURE;
  }

  signature_callback_ = new DefaultSignatureCallback(this);
  proof_source_->ComputeTlsSignature(
      server_address, client_address, hostname, signature_algorithm, in,
      std::unique_ptr<DefaultSignatureCallback>(signature_callback_));

  if (signature_callback_) {
    QUIC_DVLOG(1) << "ComputeTlsSignature is pending";
    signature_callback_->set_is_sync(false);
    return QUIC_PENDING;
  }

  bool success = handshaker_->HasValidSignature(max_signature_size);
  QUIC_DVLOG(1) << "ComputeTlsSignature completed synchronously. success:"
                << success;
  // OnComputeSignatureDone should have been called by signature_callback_->Run.
  return success ? QUIC_SUCCESS : QUIC_FAILURE;
}

TlsServerHandshaker::DecryptCallback::DecryptCallback(
    TlsServerHandshaker* handshaker)
    : handshaker_(handshaker) {}

void TlsServerHandshaker::DecryptCallback::Run(std::vector<uint8_t> plaintext) {
  if (handshaker_ == nullptr) {
    // The callback was cancelled before we could run.
    return;
  }

  TlsServerHandshaker* handshaker = handshaker_;
  handshaker_ = nullptr;

  handshaker->decrypted_session_ticket_ = std::move(plaintext);
  const bool is_async =
      (handshaker->expected_ssl_error() == SSL_ERROR_PENDING_TICKET);

  absl::optional<QuicConnectionContextSwitcher> context_switcher;

  if (is_async) {
    context_switcher.emplace(handshaker->connection_context());
  }
  QUIC_TRACESTRING(
      absl::StrCat("TLS ticket decryption done. len(decrypted_ticket):",
                   handshaker->decrypted_session_ticket_.size()));

  // DecryptCallback::Run could be called synchronously. When that happens, we
  // are currently in the middle of a call to AdvanceHandshake.
  // (AdvanceHandshake called SSL_do_handshake, which through some layers
  // called SessionTicketOpen, which called TicketCrypter::Decrypt, which
  // synchronously called this function.) In that case, the handshake will
  // continue to be processed when this function returns.
  //
  // When this callback is called asynchronously (i.e. the ticket decryption
  // is pending), TlsServerHandshaker is not actively processing handshake
  // messages. We need to have it resume processing handshake messages by
  // calling AdvanceHandshake.
  if (is_async) {
    handshaker->AdvanceHandshakeFromCallback();
  }

  handshaker->ticket_decryption_callback_ = nullptr;
}

void TlsServerHandshaker::DecryptCallback::Cancel() {
  QUICHE_DCHECK(handshaker_);
  handshaker_ = nullptr;
}

TlsServerHandshaker::TlsServerHandshaker(
    QuicSession* session, const QuicCryptoServerConfig* crypto_config)
    : TlsHandshaker(this, session),
      QuicCryptoServerStreamBase(session),
      proof_source_(crypto_config->proof_source()),
      pre_shared_key_(crypto_config->pre_shared_key()),
      crypto_negotiated_params_(new QuicCryptoNegotiatedParameters),
      tls_connection_(crypto_config->ssl_ctx(), this, session->GetSSLConfig()),
      crypto_config_(crypto_config) {
  QUIC_DVLOG(1) << "TlsServerHandshaker:  client_cert_mode initial value: "
                << client_cert_mode();

  QUICHE_DCHECK_EQ(PROTOCOL_TLS1_3,
                   session->connection()->version().handshake_protocol);

  // Configure the SSL to be a server.
  SSL_set_accept_state(ssl());

  // Make sure we use the right TLS extension codepoint.
  int use_legacy_extension = 0;
  if (session->version().UsesLegacyTlsExtension()) {
    use_legacy_extension = 1;
  }
  SSL_set_quic_use_legacy_codepoint(ssl(), use_legacy_extension);

  if (session->connection()->context()->tracer) {
    tls_connection_.EnableInfoCallback();
  }
#if BORINGSSL_API_VERSION >= 22
  if (!crypto_config->preferred_groups().empty()) {
    SSL_set1_group_ids(ssl(), crypto_config->preferred_groups().data(),
                       crypto_config->preferred_groups().size());
  }
#endif  // BORINGSSL_API_VERSION
}

TlsServerHandshaker::~TlsServerHandshaker() { CancelOutstandingCallbacks(); }

void TlsServerHandshaker::CancelOutstandingCallbacks() {
  if (proof_source_handle_) {
    proof_source_handle_->CloseHandle();
  }
  if (ticket_decryption_callback_) {
    ticket_decryption_callback_->Cancel();
    ticket_decryption_callback_ = nullptr;
  }
}

void TlsServerHandshaker::InfoCallback(int type, int value) {
  QuicConnectionTracer* tracer =
      session()->connection()->context()->tracer.get();

  if (tracer == nullptr) {
    return;
  }

  if (type & SSL_CB_LOOP) {
    tracer->PrintString(
        absl::StrCat("SSL:ACCEPT_LOOP:", SSL_state_string_long(ssl())));
  } else if (type & SSL_CB_ALERT) {
    const char* prefix =
        (type & SSL_CB_READ) ? "SSL:READ_ALERT:" : "SSL:WRITE_ALERT:";
    tracer->PrintString(absl::StrCat(prefix, SSL_alert_type_string_long(value),
                                     ":", SSL_alert_desc_string_long(value)));
  } else if (type & SSL_CB_EXIT) {
    const char* prefix =
        (value == 1) ? "SSL:ACCEPT_EXIT_OK:" : "SSL:ACCEPT_EXIT_FAIL:";
    tracer->PrintString(absl::StrCat(prefix, SSL_state_string_long(ssl())));
  } else if (type & SSL_CB_HANDSHAKE_START) {
    tracer->PrintString(
        absl::StrCat("SSL:HANDSHAKE_START:", SSL_state_string_long(ssl())));
  } else if (type & SSL_CB_HANDSHAKE_DONE) {
    tracer->PrintString(
        absl::StrCat("SSL:HANDSHAKE_DONE:", SSL_state_string_long(ssl())));
  } else {
    QUIC_DLOG(INFO) << "Unknown event type " << type << ": "
                    << SSL_state_string_long(ssl());
    tracer->PrintString(
        absl::StrCat("SSL:unknown:", value, ":", SSL_state_string_long(ssl())));
  }
}

std::unique_ptr<ProofSourceHandle>
TlsServerHandshaker::MaybeCreateProofSourceHandle() {
  return std::make_unique<DefaultProofSourceHandle>(this, proof_source_);
}

bool TlsServerHandshaker::GetBase64SHA256ClientChannelID(
    std::string* /*output*/) const {
  // Channel ID is not supported when TLS is used in QUIC.
  return false;
}

void TlsServerHandshaker::SendServerConfigUpdate(
    const CachedNetworkParameters* /*cached_network_params*/) {
  // SCUP messages aren't supported when using the TLS handshake.
}

bool TlsServerHandshaker::DisableResumption() {
  if (!can_disable_resumption_ || !session()->connection()->connected()) {
    return false;
  }
  tls_connection_.DisableTicketSupport();
  return true;
}

bool TlsServerHandshaker::IsZeroRtt() const {
  return SSL_early_data_accepted(ssl());
}

bool TlsServerHandshaker::IsResumption() const {
  return SSL_session_reused(ssl());
}

bool TlsServerHandshaker::ResumptionAttempted() const {
  return ticket_received_;
}

bool TlsServerHandshaker::EarlyDataAttempted() const {
  QUIC_BUG_IF(quic_tls_early_data_attempted_too_early,
              !select_cert_status_.has_value())
      << "EarlyDataAttempted must be called after EarlySelectCertCallback is "
         "started";
  return early_data_attempted_;
}

int TlsServerHandshaker::NumServerConfigUpdateMessagesSent() const {
  // SCUP messages aren't supported when using the TLS handshake.
  return 0;
}

const CachedNetworkParameters*
TlsServerHandshaker::PreviousCachedNetworkParams() const {
  return last_received_cached_network_params_.get();
}

void TlsServerHandshaker::SetPreviousCachedNetworkParams(
    CachedNetworkParameters cached_network_params) {
  last_received_cached_network_params_ =
      std::make_unique<CachedNetworkParameters>(cached_network_params);
}

void TlsServerHandshaker::OnPacketDecrypted(EncryptionLevel level) {
  if (level == ENCRYPTION_HANDSHAKE && state_ < HANDSHAKE_PROCESSED) {
    state_ = HANDSHAKE_PROCESSED;
    handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_INITIAL);
    handshaker_delegate()->DiscardOldDecryptionKey(ENCRYPTION_INITIAL);
  }
}

void TlsServerHandshaker::OnHandshakeDoneReceived() { QUICHE_DCHECK(false); }

void TlsServerHandshaker::OnNewTokenReceived(absl::string_view /*token*/) {
  QUICHE_DCHECK(false);
}

std::string TlsServerHandshaker::GetAddressToken(
    const CachedNetworkParameters* cached_network_params) const {
  SourceAddressTokens empty_previous_tokens;
  const QuicConnection* connection = session()->connection();
  return crypto_config_->NewSourceAddressToken(
      crypto_config_->source_address_token_boxer(), empty_previous_tokens,
      connection->effective_peer_address().host(),
      connection->random_generator(), connection->clock()->WallNow(),
      cached_network_params);
}

bool TlsServerHandshaker::ValidateAddressToken(absl::string_view token) const {
  SourceAddressTokens tokens;
  HandshakeFailureReason reason = crypto_config_->ParseSourceAddressToken(
      crypto_config_->source_address_token_boxer(), token, tokens);
  if (reason != HANDSHAKE_OK) {
    QUIC_DLOG(WARNING) << "Failed to parse source address token: "
                       << CryptoUtils::HandshakeFailureReasonToString(reason);
    return false;
  }
  auto cached_network_params = std::make_unique<CachedNetworkParameters>();
  reason = crypto_config_->ValidateSourceAddressTokens(
      tokens, session()->connection()->effective_peer_address().host(),
      session()->connection()->clock()->WallNow(), cached_network_params.get());
  if (reason != HANDSHAKE_OK) {
    QUIC_DLOG(WARNING) << "Failed to validate source address token: "
                       << CryptoUtils::HandshakeFailureReasonToString(reason);
    return false;
  }

  last_received_cached_network_params_ = std::move(cached_network_params);
  return true;
}

bool TlsServerHandshaker::ShouldSendExpectCTHeader() const { return false; }

bool TlsServerHandshaker::DidCertMatchSni() const { return cert_matched_sni_; }

const ProofSource::Details* TlsServerHandshaker::ProofSourceDetails() const {
  return proof_source_details_.get();
}

bool TlsServerHandshaker::ExportKeyingMaterial(absl::string_view label,
                                               absl::string_view context,
                                               size_t result_len,
                                               std::string* result) {
  return ExportKeyingMaterialForLabel(label, context, result_len, result);
}

void TlsServerHandshaker::OnConnectionClosed(QuicErrorCode error,
                                             ConnectionCloseSource source) {
  TlsHandshaker::OnConnectionClosed(error, source);
}

ssl_early_data_reason_t TlsServerHandshaker::EarlyDataReason() const {
  return TlsHandshaker::EarlyDataReason();
}

bool TlsServerHandshaker::encryption_established() const {
  return encryption_established_;
}

bool TlsServerHandshaker::one_rtt_keys_available() const {
  return state_ == HANDSHAKE_CONFIRMED;
}

const QuicCryptoNegotiatedParameters&
TlsServerHandshaker::crypto_negotiated_params() const {
  return *crypto_negotiated_params_;
}

CryptoMessageParser* TlsServerHandshaker::crypto_message_parser() {
  return TlsHandshaker::crypto_message_parser();
}

HandshakeState TlsServerHandshaker::GetHandshakeState() const { return state_; }

void TlsServerHandshaker::SetServerApplicationStateForResumption(
    std::unique_ptr<ApplicationState> state) {
  application_state_ = std::move(state);
}

size_t TlsServerHandshaker::BufferSizeLimitForLevel(
    EncryptionLevel level) const {
  return TlsHandshaker::BufferSizeLimitForLevel(level);
}

std::unique_ptr<QuicDecrypter>
TlsServerHandshaker::AdvanceKeysAndCreateCurrentOneRttDecrypter() {
  return TlsHandshaker::AdvanceKeysAndCreateCurrentOneRttDecrypter();
}

std::unique_ptr<QuicEncrypter>
TlsServerHandshaker::CreateCurrentOneRttEncrypter() {
  return TlsHandshaker::CreateCurrentOneRttEncrypter();
}

void TlsServerHandshaker::OverrideQuicConfigDefaults(QuicConfig* /*config*/) {}

void TlsServerHandshaker::AdvanceHandshakeFromCallback() {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());

  AdvanceHandshake();
  if (!is_connection_closed()) {
    handshaker_delegate()->OnHandshakeCallbackDone();
  }
}

bool TlsServerHandshaker::ProcessTransportParameters(
    const SSL_CLIENT_HELLO* client_hello, std::string* error_details) {
  TransportParameters client_params;
  const uint8_t* client_params_bytes;
  size_t params_bytes_len;

  // Make sure we use the right TLS extension codepoint.
  uint16_t extension_type = TLSEXT_TYPE_quic_transport_parameters_standard;
  if (session()->version().UsesLegacyTlsExtension()) {
    extension_type = TLSEXT_TYPE_quic_transport_parameters_legacy;
  }
  // When using early select cert callback, SSL_get_peer_quic_transport_params
  // can not be used to retrieve the client's transport parameters, but we can
  // use SSL_early_callback_ctx_extension_get to do that.
  if (!SSL_early_callback_ctx_extension_get(client_hello, extension_type,
                                            &client_params_bytes,
                                            &params_bytes_len)) {
    params_bytes_len = 0;
  }

  if (params_bytes_len == 0) {
    *error_details = "Client's transport parameters are missing";
    return false;
  }
  std::string parse_error_details;
  if (!ParseTransportParameters(session()->connection()->version(),
                                Perspective::IS_CLIENT, client_params_bytes,
                                params_bytes_len, &client_params,
                                &parse_error_details)) {
    QUICHE_DCHECK(!parse_error_details.empty());
    *error_details =
        "Unable to parse client's transport parameters: " + parse_error_details;
    return false;
  }

  // Notify QuicConnectionDebugVisitor.
  session()->connection()->OnTransportParametersReceived(client_params);

  if (client_params.legacy_version_information.has_value() &&
      CryptoUtils::ValidateClientHelloVersion(
          client_params.legacy_version_information->version,
          session()->connection()->version(), session()->supported_versions(),
          error_details) != QUIC_NO_ERROR) {
    return false;
  }

  if (client_params.version_information.has_value() &&
      !CryptoUtils::ValidateChosenVersion(
          client_params.version_information->chosen_version,
          session()->version(), error_details)) {
    QUICHE_DCHECK(!error_details->empty());
    return false;
  }

  if (handshaker_delegate()->ProcessTransportParameters(
          client_params, /* is_resumption = */ false, error_details) !=
      QUIC_NO_ERROR) {
    return false;
  }

  ProcessAdditionalTransportParameters(client_params);

  return true;
}

TlsServerHandshaker::SetTransportParametersResult
TlsServerHandshaker::SetTransportParameters() {
  SetTransportParametersResult result;
  QUICHE_DCHECK(!result.success);

  server_params_.perspective = Perspective::IS_SERVER;
  server_params_.legacy_version_information =
      TransportParameters::LegacyVersionInformation();
  server_params_.legacy_version_information->supported_versions =
      CreateQuicVersionLabelVector(session()->supported_versions());
  server_params_.legacy_version_information->version =
      CreateQuicVersionLabel(session()->connection()->version());
  server_params_.version_information =
      TransportParameters::VersionInformation();
  server_params_.version_information->chosen_version =
      CreateQuicVersionLabel(session()->version());
  server_params_.version_information->other_versions =
      CreateQuicVersionLabelVector(session()->supported_versions());

  if (!handshaker_delegate()->FillTransportParameters(&server_params_)) {
    return result;
  }

  // Notify QuicConnectionDebugVisitor.
  session()->connection()->OnTransportParametersSent(server_params_);

  {  // Ensure |server_params_bytes| is not accessed out of the scope.
    std::vector<uint8_t> server_params_bytes;
    if (!SerializeTransportParameters(server_params_, &server_params_bytes) ||
        SSL_set_quic_transport_params(ssl(), server_params_bytes.data(),
                                      server_params_bytes.size()) != 1) {
      return result;
    }
    result.quic_transport_params = std::move(server_params_bytes);
  }

  if (application_state_) {
    std::vector<uint8_t> early_data_context;
    if (!SerializeTransportParametersForTicket(
            server_params_, *application_state_, &early_data_context)) {
      QUIC_BUG(quic_bug_10341_4)
          << "Failed to serialize Transport Parameters for ticket.";
      result.early_data_context = std::vector<uint8_t>();
      return result;
    }
    SSL_set_quic_early_data_context(ssl(), early_data_context.data(),
                                    early_data_context.size());
    result.early_data_context = std::move(early_data_context);
    application_state_.reset(nullptr);
  }
  result.success = true;
  return result;
}

bool TlsServerHandshaker::TransportParametersMatch(
    absl::Span<const uint8_t> serialized_params) const {
  TransportParameters params;
  std::string error_details;

  bool parse_ok = ParseTransportParameters(
      session()->version(), Perspective::IS_SERVER, serialized_params.data(),
      serialized_params.size(), &params, &error_details);

  if (!parse_ok) {
    return false;
  }

  DegreaseTransportParameters(params);

  return params == server_params_;
}

void TlsServerHandshaker::SetWriteSecret(
    EncryptionLevel level, const SSL_CIPHER* cipher,
    absl::Span<const uint8_t> write_secret) {
  if (is_connection_closed()) {
    return;
  }
  if (level == ENCRYPTION_FORWARD_SECURE) {
    encryption_established_ = true;
    // Fill crypto_negotiated_params_:
    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl());
    if (cipher) {
      crypto_negotiated_params_->cipher_suite =
          SSL_CIPHER_get_protocol_id(cipher);
    }
    crypto_negotiated_params_->key_exchange_group = SSL_get_curve_id(ssl());
    crypto_negotiated_params_->encrypted_client_hello = SSL_ech_accepted(ssl());
  }
  TlsHandshaker::SetWriteSecret(level, cipher, write_secret);
}

std::string TlsServerHandshaker::GetAcceptChValueForHostname(
    const std::string& /*hostname*/) const {
  return {};
}

void TlsServerHandshaker::FinishHandshake() {
  QUICHE_DCHECK(!SSL_in_early_data(ssl()));

  if (!valid_alpn_received_) {
    QUIC_DLOG(ERROR)
        << "Server: handshake finished without receiving a known ALPN";
    // TODO(b/130164908) this should send no_application_protocol
    // instead of QUIC_HANDSHAKE_FAILED.
    CloseConnection(QUIC_HANDSHAKE_FAILED,
                    "Server did not receive a known ALPN");
    return;
  }

  ssl_early_data_reason_t reason_code = EarlyDataReason();
  QUIC_DLOG(INFO) << "Server: handshake finished. Early data reason "
                  << reason_code << " ("
                  << CryptoUtils::EarlyDataReasonToString(reason_code) << ")";
  state_ = HANDSHAKE_CONFIRMED;

  handshaker_delegate()->OnTlsHandshakeComplete();
  handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_HANDSHAKE);
  handshaker_delegate()->DiscardOldDecryptionKey(ENCRYPTION_HANDSHAKE);
  // ENCRYPTION_ZERO_RTT decryption key is not discarded here as "Servers MAY
  // temporarily retain 0-RTT keys to allow decrypting reordered packets
  // without requiring their contents to be retransmitted with 1-RTT keys."
  // It is expected that QuicConnection will discard the key at an
  // appropriate time.
}

QuicAsyncStatus TlsServerHandshaker::VerifyCertChain(
    const std::vector<std::string>& /*certs*/, std::string* /*error_details*/,
    std::unique_ptr<ProofVerifyDetails>* /*details*/, uint8_t* /*out_alert*/,
    std::unique_ptr<ProofVerifierCallback> /*callback*/) {
  QUIC_DVLOG(1) << "VerifyCertChain returning success";

  // No real verification here. A subclass can override this function to verify
  // the client cert if needed.
  return QUIC_SUCCESS;
}

void TlsServerHandshaker::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& /*verify_details*/) {}

ssl_private_key_result_t TlsServerHandshaker::PrivateKeySign(
    uint8_t* out, size_t* out_len, size_t max_out, uint16_t sig_alg,
    absl::string_view in) {
  QUICHE_DCHECK_EQ(expected_ssl_error(), SSL_ERROR_WANT_READ);

  QuicAsyncStatus status = proof_source_handle_->ComputeSignature(
      session()->connection()->self_address(),
      session()->connection()->peer_address(), crypto_negotiated_params_->sni,
      sig_alg, in, max_out);
  if (status == QUIC_PENDING) {
    set_expected_ssl_error(SSL_ERROR_WANT_PRIVATE_KEY_OPERATION);
    if (async_op_timer_.has_value()) {
      QUIC_CODE_COUNT(
          quic_tls_server_computing_signature_while_another_op_pending);
    }
    async_op_timer_ = QuicTimeAccumulator();
    async_op_timer_->Start(now());
  }
  return PrivateKeyComplete(out, out_len, max_out);
}

ssl_private_key_result_t TlsServerHandshaker::PrivateKeyComplete(
    uint8_t* out, size_t* out_len, size_t max_out) {
  if (expected_ssl_error() == SSL_ERROR_WANT_PRIVATE_KEY_OPERATION) {
    return ssl_private_key_retry;
  }

  const bool success = HasValidSignature(max_out);
  QuicConnectionStats::TlsServerOperationStats compute_signature_stats;
  compute_signature_stats.success = success;
  if (async_op_timer_.has_value()) {
    async_op_timer_->Stop(now());
    compute_signature_stats.async_latency =
        async_op_timer_->GetTotalElapsedTime();
    async_op_timer_.reset();
    RECORD_LATENCY_IN_US("tls_server_async_compute_signature_latency_us",
                         compute_signature_stats.async_latency,
                         "Async compute signature latency in microseconds");
  }
  connection_stats().tls_server_compute_signature_stats =
      std::move(compute_signature_stats);

  if (!success) {
    return ssl_private_key_failure;
  }
  *out_len = cert_verify_sig_.size();
  memcpy(out, cert_verify_sig_.data(), *out_len);
  cert_verify_sig_.clear();
  cert_verify_sig_.shrink_to_fit();
  return ssl_private_key_success;
}

void TlsServerHandshaker::OnComputeSignatureDone(
    bool ok, bool is_sync, std::string signature,
    std::unique_ptr<ProofSource::Details> details) {
  QUIC_DVLOG(1) << "OnComputeSignatureDone. ok:" << ok
                << ", is_sync:" << is_sync
                << ", len(signature):" << signature.size();
  absl::optional<QuicConnectionContextSwitcher> context_switcher;

  if (!is_sync) {
    context_switcher.emplace(connection_context());
  }

  QUIC_TRACESTRING(absl::StrCat("TLS compute signature done. ok:", ok,
                                ", len(signature):", signature.size()));

  if (ok) {
    cert_verify_sig_ = std::move(signature);
    proof_source_details_ = std::move(details);
  }
  const int last_expected_ssl_error = expected_ssl_error();
  set_expected_ssl_error(SSL_ERROR_WANT_READ);
  if (!is_sync) {
    QUICHE_DCHECK_EQ(last_expected_ssl_error,
                     SSL_ERROR_WANT_PRIVATE_KEY_OPERATION);
    AdvanceHandshakeFromCallback();
  }
}

bool TlsServerHandshaker::HasValidSignature(size_t max_signature_size) const {
  return !cert_verify_sig_.empty() &&
         cert_verify_sig_.size() <= max_signature_size;
}

size_t TlsServerHandshaker::SessionTicketMaxOverhead() {
  QUICHE_DCHECK(proof_source_->GetTicketCrypter());
  return proof_source_->GetTicketCrypter()->MaxOverhead();
}

int TlsServerHandshaker::SessionTicketSeal(uint8_t* out, size_t* out_len,
                                           size_t max_out_len,
                                           absl::string_view in) {
  QUICHE_DCHECK(proof_source_->GetTicketCrypter());
  std::vector<uint8_t> ticket =
      proof_source_->GetTicketCrypter()->Encrypt(in, ticket_encryption_key_);
  if (GetQuicReloadableFlag(
          quic_send_placeholder_ticket_when_encrypt_ticket_fails) &&
      ticket.empty()) {
    QUIC_CODE_COUNT(quic_tls_server_handshaker_send_placeholder_ticket);
    const absl::string_view kTicketFailurePlaceholder = "TICKET FAILURE";
    const absl::string_view kTicketWithSizeLimit =
        kTicketFailurePlaceholder.substr(0, max_out_len);
    ticket.assign(kTicketWithSizeLimit.begin(), kTicketWithSizeLimit.end());
  }
  if (max_out_len < ticket.size()) {
    QUIC_BUG(quic_bug_12423_2)
        << "TicketCrypter returned " << ticket.size()
        << " bytes of ciphertext, which is larger than its max overhead of "
        << max_out_len;
    return 0;  // failure
  }
  *out_len = ticket.size();
  memcpy(out, ticket.data(), ticket.size());
  QUIC_CODE_COUNT(quic_tls_server_handshaker_tickets_sealed);
  return 1;  // success
}

ssl_ticket_aead_result_t TlsServerHandshaker::SessionTicketOpen(
    uint8_t* out, size_t* out_len, size_t max_out_len, absl::string_view in) {
  QUICHE_DCHECK(proof_source_->GetTicketCrypter());

  if (ignore_ticket_open_) {
    // SetIgnoreTicketOpen has been called. Typically this means the caller is
    // using handshake hints and expect the hints to contain ticket decryption
    // results.
    QUIC_CODE_COUNT(quic_tls_server_handshaker_tickets_ignored_1);
    return ssl_ticket_aead_ignore_ticket;
  }

  if (!ticket_decryption_callback_) {
    ticket_decryption_callback_ = std::make_shared<DecryptCallback>(this);
    proof_source_->GetTicketCrypter()->Decrypt(in, ticket_decryption_callback_);

    // Decrypt can run the callback synchronously. In that case, the callback
    // will clear the ticket_decryption_callback_ pointer, and instead of
    // returning ssl_ticket_aead_retry, we should continue processing to
    // return the decrypted ticket.
    //
    // If the callback is not run synchronously, return ssl_ticket_aead_retry
    // and when the callback is complete this function will be run again to
    // return the result.
    if (ticket_decryption_callback_) {
      QUICHE_DCHECK(!ticket_decryption_callback_->IsDone());
      set_expected_ssl_error(SSL_ERROR_PENDING_TICKET);
      if (async_op_timer_.has_value()) {
        QUIC_CODE_COUNT(
            quic_tls_server_decrypting_ticket_while_another_op_pending);
      }
      async_op_timer_ = QuicTimeAccumulator();
      async_op_timer_->Start(now());
    }
  }

  // If the async ticket decryption is pending, either started by this
  // SessionTicketOpen call or one that happened earlier, return
  // ssl_ticket_aead_retry.
  if (ticket_decryption_callback_ && !ticket_decryption_callback_->IsDone()) {
    return ssl_ticket_aead_retry;
  }

  ssl_ticket_aead_result_t result =
      FinalizeSessionTicketOpen(out, out_len, max_out_len);

  QuicConnectionStats::TlsServerOperationStats decrypt_ticket_stats;
  decrypt_ticket_stats.success = (result == ssl_ticket_aead_success);
  if (async_op_timer_.has_value()) {
    async_op_timer_->Stop(now());
    decrypt_ticket_stats.async_latency = async_op_timer_->GetTotalElapsedTime();
    async_op_timer_.reset();
    RECORD_LATENCY_IN_US("tls_server_async_decrypt_ticket_latency_us",
                         decrypt_ticket_stats.async_latency,
                         "Async decrypt ticket latency in microseconds");
  }
  connection_stats().tls_server_decrypt_ticket_stats =
      std::move(decrypt_ticket_stats);

  return result;
}

ssl_ticket_aead_result_t TlsServerHandshaker::FinalizeSessionTicketOpen(
    uint8_t* out, size_t* out_len, size_t max_out_len) {
  ticket_decryption_callback_ = nullptr;
  set_expected_ssl_error(SSL_ERROR_WANT_READ);
  if (decrypted_session_ticket_.empty()) {
    QUIC_DLOG(ERROR) << "Session ticket decryption failed; ignoring ticket";
    // Ticket decryption failed. Ignore the ticket.
    QUIC_CODE_COUNT(quic_tls_server_handshaker_tickets_ignored_2);
    return ssl_ticket_aead_ignore_ticket;
  }
  if (max_out_len < decrypted_session_ticket_.size()) {
    return ssl_ticket_aead_error;
  }
  memcpy(out, decrypted_session_ticket_.data(),
         decrypted_session_ticket_.size());
  *out_len = decrypted_session_ticket_.size();

  QUIC_CODE_COUNT(quic_tls_server_handshaker_tickets_opened);
  return ssl_ticket_aead_success;
}

ssl_select_cert_result_t TlsServerHandshaker::EarlySelectCertCallback(
    const SSL_CLIENT_HELLO* client_hello) {
  // EarlySelectCertCallback can be called twice from BoringSSL: If the first
  // call returns ssl_select_cert_retry, when cert selection completes,
  // SSL_do_handshake will call it again.

  if (select_cert_status_.has_value()) {
    // This is the second call, return the result directly.
    QUIC_DVLOG(1) << "EarlySelectCertCallback called to continue handshake, "
                     "returning directly. success:"
                  << (*select_cert_status_ == QUIC_SUCCESS);
    return (*select_cert_status_ == QUIC_SUCCESS) ? ssl_select_cert_success
                                                  : ssl_select_cert_error;
  }

  // This is the first call.
  select_cert_status_ = QUIC_PENDING;
  proof_source_handle_ = MaybeCreateProofSourceHandle();

  if (!pre_shared_key_.empty()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    QUIC_BUG(quic_bug_10341_6)
        << "QUIC server pre-shared keys not yet supported with TLS";
    return ssl_select_cert_error;
  }

  {
    const uint8_t* unused_extension_bytes;
    size_t unused_extension_len;
    ticket_received_ = SSL_early_callback_ctx_extension_get(
        client_hello, TLSEXT_TYPE_pre_shared_key, &unused_extension_bytes,
        &unused_extension_len);

    early_data_attempted_ = SSL_early_callback_ctx_extension_get(
        client_hello, TLSEXT_TYPE_early_data, &unused_extension_bytes,
        &unused_extension_len);
  }

  // This callback is called very early by Boring SSL, most of the SSL_get_foo
  // function do not work at this point, but SSL_get_servername does.
  const char* hostname = SSL_get_servername(ssl(), TLSEXT_NAMETYPE_host_name);
  if (hostname) {
    crypto_negotiated_params_->sni =
        QuicHostnameUtils::NormalizeHostname(hostname);
    if (!ValidateHostname(hostname)) {
      return ssl_select_cert_error;
    }
    if (hostname != crypto_negotiated_params_->sni) {
      QUIC_CODE_COUNT(quic_tls_server_hostname_diff);
      QUIC_LOG_EVERY_N_SEC(WARNING, 300)
          << "Raw and normalized hostnames differ, but both are valid SNIs. "
             "raw hostname:"
          << hostname << ", normalized:" << crypto_negotiated_params_->sni;
    } else {
      QUIC_CODE_COUNT(quic_tls_server_hostname_same);
    }
  } else {
    QUIC_LOG(INFO) << "No hostname indicated in SNI";
  }

  std::string error_details;
  if (!ProcessTransportParameters(client_hello, &error_details)) {
    CloseConnection(QUIC_HANDSHAKE_FAILED, error_details);
    return ssl_select_cert_error;
  }
  OverrideQuicConfigDefaults(session()->config());
  session()->OnConfigNegotiated();

  auto set_transport_params_result = SetTransportParameters();
  if (!set_transport_params_result.success) {
    QUIC_LOG(ERROR) << "Failed to set transport parameters";
    return ssl_select_cert_error;
  }

  bssl::UniquePtr<uint8_t> ssl_capabilities;
  size_t ssl_capabilities_len = 0;
  absl::string_view ssl_capabilities_view;

  if (CryptoUtils::GetSSLCapabilities(ssl(), &ssl_capabilities,
                                      &ssl_capabilities_len)) {
    ssl_capabilities_view =
        absl::string_view(reinterpret_cast<const char*>(ssl_capabilities.get()),
                          ssl_capabilities_len);
  }

  // Enable ALPS for the session's ALPN.
  SetApplicationSettingsResult alps_result =
      SetApplicationSettings(AlpnForVersion(session()->version()));
  if (!alps_result.success) {
    return ssl_select_cert_error;
  }

  if (!session()->connection()->connected()) {
    select_cert_status_ = QUIC_FAILURE;
    return ssl_select_cert_error;
  }

  can_disable_resumption_ = false;
  const QuicAsyncStatus status = proof_source_handle_->SelectCertificate(
      session()->connection()->self_address().Normalized(),
      session()->connection()->peer_address().Normalized(),
      session()->connection()->GetOriginalDestinationConnectionId(),
      ssl_capabilities_view, crypto_negotiated_params_->sni,
      absl::string_view(
          reinterpret_cast<const char*>(client_hello->client_hello),
          client_hello->client_hello_len),
      AlpnForVersion(session()->version()), std::move(alps_result.alps_buffer),
      set_transport_params_result.quic_transport_params,
      set_transport_params_result.early_data_context,
      tls_connection_.ssl_config());

  QUICHE_DCHECK_EQ(status, *select_cert_status());

  if (status == QUIC_PENDING) {
    set_expected_ssl_error(SSL_ERROR_PENDING_CERTIFICATE);
    if (async_op_timer_.has_value()) {
      QUIC_CODE_COUNT(quic_tls_server_selecting_cert_while_another_op_pending);
    }
    async_op_timer_ = QuicTimeAccumulator();
    async_op_timer_->Start(now());
    return ssl_select_cert_retry;
  }

  if (status == QUIC_FAILURE) {
    return ssl_select_cert_error;
  }

  return ssl_select_cert_success;
}

void TlsServerHandshaker::OnSelectCertificateDone(
    bool ok, bool is_sync, const ProofSource::Chain* chain,
    absl::string_view handshake_hints, absl::string_view ticket_encryption_key,
    bool cert_matched_sni, QuicDelayedSSLConfig delayed_ssl_config) {
  QUIC_DVLOG(1) << "OnSelectCertificateDone. ok:" << ok
                << ", is_sync:" << is_sync
                << ", len(handshake_hints):" << handshake_hints.size()
                << ", len(ticket_encryption_key):"
                << ticket_encryption_key.size();
  absl::optional<QuicConnectionContextSwitcher> context_switcher;
  if (!is_sync) {
    context_switcher.emplace(connection_context());
  }

  QUIC_TRACESTRING(absl::StrCat(
      "TLS select certificate done: ok:", ok,
      ", certs_found:", (chain != nullptr && !chain->certs.empty()),
      ", len(handshake_hints):", handshake_hints.size(),
      ", len(ticket_encryption_key):", ticket_encryption_key.size()));

  ticket_encryption_key_ = std::string(ticket_encryption_key);
  select_cert_status_ = QUIC_FAILURE;
  cert_matched_sni_ = cert_matched_sni;

  if (delayed_ssl_config.quic_transport_parameters.has_value()) {
    // In case of any error the SSL object is still valid. Handshaker may need
    // to call ComputeSignature but otherwise can proceed.
    if (TransportParametersMatch(
            absl::MakeSpan(*delayed_ssl_config.quic_transport_parameters))) {
      if (SSL_set_quic_transport_params(
              ssl(), delayed_ssl_config.quic_transport_parameters->data(),
              delayed_ssl_config.quic_transport_parameters->size()) != 1) {
        QUIC_DVLOG(1) << "SSL_set_quic_transport_params override failed";
      }
    } else {
      QUIC_DVLOG(1)
          << "QUIC transport parameters mismatch with ProofSourceHandle";
    }
  }

  if (delayed_ssl_config.client_cert_mode.has_value()) {
    tls_connection_.SetClientCertMode(*delayed_ssl_config.client_cert_mode);
    QUIC_DVLOG(1) << "client_cert_mode after cert selection: "
                  << client_cert_mode();
  }

  if (ok) {
    if (chain && !chain->certs.empty()) {
      tls_connection_.SetCertChain(chain->ToCryptoBuffers().value);
      if (!handshake_hints.empty() &&
          !SSL_set_handshake_hints(
              ssl(), reinterpret_cast<const uint8_t*>(handshake_hints.data()),
              handshake_hints.size())) {
        // If |SSL_set_handshake_hints| fails, the ssl() object will remain
        // intact, it is as if we didn't call it. The handshaker will
        // continue to compute signature/decrypt ticket as normal.
        QUIC_CODE_COUNT(quic_tls_server_set_handshake_hints_failed);
        QUIC_DVLOG(1) << "SSL_set_handshake_hints failed";
      }
      select_cert_status_ = QUIC_SUCCESS;
    } else {
      QUIC_DLOG(ERROR) << "No certs provided for host '"
                       << crypto_negotiated_params_->sni << "', server_address:"
                       << session()->connection()->self_address()
                       << ", client_address:"
                       << session()->connection()->peer_address();
    }
  }

  QuicConnectionStats::TlsServerOperationStats select_cert_stats;
  select_cert_stats.success = (select_cert_status_ == QUIC_SUCCESS);
  QUICHE_DCHECK_NE(is_sync, async_op_timer_.has_value());
  if (async_op_timer_.has_value()) {
    async_op_timer_->Stop(now());
    select_cert_stats.async_latency = async_op_timer_->GetTotalElapsedTime();
    async_op_timer_.reset();
    RECORD_LATENCY_IN_US("tls_server_async_select_cert_latency_us",
                         select_cert_stats.async_latency,
                         "Async select cert latency in microseconds");
  }
  connection_stats().tls_server_select_cert_stats =
      std::move(select_cert_stats);

  const int last_expected_ssl_error = expected_ssl_error();
  set_expected_ssl_error(SSL_ERROR_WANT_READ);
  if (!is_sync) {
    QUICHE_DCHECK_EQ(last_expected_ssl_error, SSL_ERROR_PENDING_CERTIFICATE);
    AdvanceHandshakeFromCallback();
  }
}

bool TlsServerHandshaker::WillNotCallComputeSignature() const {
  return SSL_can_release_private_key(ssl());
}

bool TlsServerHandshaker::ValidateHostname(const std::string& hostname) const {
  if (!QuicHostnameUtils::IsValidSNI(hostname)) {
    // TODO(b/151676147): Include this error string in the CONNECTION_CLOSE
    // frame.
    QUIC_DLOG(ERROR) << "Invalid SNI provided: \"" << hostname << "\"";
    return false;
  }
  return true;
}

int TlsServerHandshaker::TlsExtServernameCallback(int* /*out_alert*/) {
  // SSL_TLSEXT_ERR_OK causes the server_name extension to be acked in
  // ServerHello.
  return SSL_TLSEXT_ERR_OK;
}

int TlsServerHandshaker::SelectAlpn(const uint8_t** out, uint8_t* out_len,
                                    const uint8_t* in, unsigned in_len) {
  // |in| contains a sequence of 1-byte-length-prefixed values.
  *out_len = 0;
  *out = nullptr;
  if (in_len == 0) {
    QUIC_DLOG(ERROR) << "No ALPN provided by client";
    return SSL_TLSEXT_ERR_NOACK;
  }

  CBS all_alpns;
  CBS_init(&all_alpns, in, in_len);

  std::vector<absl::string_view> alpns;
  while (CBS_len(&all_alpns) > 0) {
    CBS alpn;
    if (!CBS_get_u8_length_prefixed(&all_alpns, &alpn)) {
      QUIC_DLOG(ERROR) << "Failed to parse ALPN length";
      return SSL_TLSEXT_ERR_NOACK;
    }

    const size_t alpn_length = CBS_len(&alpn);
    if (alpn_length == 0) {
      QUIC_DLOG(ERROR) << "Received invalid zero-length ALPN";
      return SSL_TLSEXT_ERR_NOACK;
    }

    alpns.emplace_back(reinterpret_cast<const char*>(CBS_data(&alpn)),
                       alpn_length);
  }

  // TODO(wub): Remove QuicSession::SelectAlpn. QuicSessions should know the
  // ALPN on construction.
  auto selected_alpn = session()->SelectAlpn(alpns);
  if (selected_alpn == alpns.end()) {
    QUIC_DLOG(ERROR) << "No known ALPN provided by client";
    return SSL_TLSEXT_ERR_NOACK;
  }

  session()->OnAlpnSelected(*selected_alpn);
  valid_alpn_received_ = true;
  *out_len = selected_alpn->size();
  *out = reinterpret_cast<const uint8_t*>(selected_alpn->data());
  return SSL_TLSEXT_ERR_OK;
}

TlsServerHandshaker::SetApplicationSettingsResult
TlsServerHandshaker::SetApplicationSettings(absl::string_view alpn) {
  TlsServerHandshaker::SetApplicationSettingsResult result;

  const std::string& hostname = crypto_negotiated_params_->sni;
  std::string accept_ch_value = GetAcceptChValueForHostname(hostname);
  std::string origin = absl::StrCat("https://", hostname);
  uint16_t port = session()->self_address().port();
  if (port != kDefaultPort) {
    // This should be rare in production, but useful for test servers.
    QUIC_CODE_COUNT(quic_server_alps_non_default_port);
    absl::StrAppend(&origin, ":", port);
  }

  if (!accept_ch_value.empty()) {
    AcceptChFrame frame{{{std::move(origin), std::move(accept_ch_value)}}};
    result.alps_buffer = HttpEncoder::SerializeAcceptChFrame(frame);
  }

  const std::string& alps = result.alps_buffer;
  if (SSL_add_application_settings(
          ssl(), reinterpret_cast<const uint8_t*>(alpn.data()), alpn.size(),
          reinterpret_cast<const uint8_t*>(alps.data()), alps.size()) != 1) {
    QUIC_DLOG(ERROR) << "Failed to enable ALPS";
    result.success = false;
  } else {
    result.success = true;
  }
  return result;
}

SSL* TlsServerHandshaker::GetSsl() const { return ssl(); }

bool TlsServerHandshaker::IsCryptoFrameExpectedForEncryptionLevel(
    EncryptionLevel level) const {
  return level != ENCRYPTION_ZERO_RTT;
}

EncryptionLevel TlsServerHandshaker::GetEncryptionLevelToSendCryptoDataOfSpace(
    PacketNumberSpace space) const {
  switch (space) {
    case INITIAL_DATA:
      return ENCRYPTION_INITIAL;
    case HANDSHAKE_DATA:
      return ENCRYPTION_HANDSHAKE;
    case APPLICATION_DATA:
      return ENCRYPTION_FORWARD_SECURE;
    default:
      QUICHE_DCHECK(false);
      return NUM_ENCRYPTION_LEVELS;
  }
}

}  // namespace quic
