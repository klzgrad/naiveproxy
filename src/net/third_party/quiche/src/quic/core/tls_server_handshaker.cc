// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/tls_server_handshaker.h"

#include <memory>
#include <string>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "quic/core/crypto/quic_crypto_server_config.h"
#include "quic/core/crypto/transport_parameters.h"
#include "quic/core/http/http_encoder.h"
#include "quic/core/http/http_frames.h"
#include "quic/core/quic_time.h"
#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_flag_utils.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_hostname_utils.h"
#include "quic/platform/api/quic_logging.h"
#include "common/platform/api/quiche_text_utils.h"

namespace quic {

TlsServerHandshaker::DefaultProofSourceHandle::DefaultProofSourceHandle(
    TlsServerHandshaker* handshaker,
    ProofSource* proof_source)
    : handshaker_(handshaker), proof_source_(proof_source) {}

TlsServerHandshaker::DefaultProofSourceHandle::~DefaultProofSourceHandle() {
  CancelPendingOperation();
}

void TlsServerHandshaker::DefaultProofSourceHandle::CancelPendingOperation() {
  QUIC_DVLOG(1) << "CancelPendingOperation. is_signature_pending="
                << (signature_callback_ != nullptr);
  if (signature_callback_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_tls_use_per_handshaker_proof_source, 3,
                                 3);
    signature_callback_->Cancel();
    signature_callback_ = nullptr;
  }
}

QuicAsyncStatus
TlsServerHandshaker::DefaultProofSourceHandle::SelectCertificate(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address,
    const std::string& hostname,
    absl::string_view /*client_hello*/,
    const std::string& /*alpn*/,
    const std::vector<uint8_t>& /*quic_transport_params*/,
    const absl::optional<std::vector<uint8_t>>& /*early_data_context*/) {
  if (!handshaker_ || !proof_source_) {
    QUIC_BUG << "SelectCertificate called on a detached handle";
    return QUIC_FAILURE;
  }

  QuicReferenceCountedPointer<ProofSource::Chain> chain =
      proof_source_->GetCertChain(server_address, client_address, hostname);

  handshaker_->OnSelectCertificateDone(
      /*ok=*/true, /*is_sync=*/true, chain.get());
  if (!handshaker_->select_cert_status().has_value()) {
    QUIC_BUG
        << "select_cert_status() has no value after a synchronous select cert";
    // Return success to continue the handshake.
    return QUIC_SUCCESS;
  }
  return handshaker_->select_cert_status().value();
}

QuicAsyncStatus TlsServerHandshaker::DefaultProofSourceHandle::ComputeSignature(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address,
    const std::string& hostname,
    uint16_t signature_algorithm,
    absl::string_view in,
    size_t max_signature_size) {
  if (!handshaker_ || !proof_source_) {
    QUIC_BUG << "ComputeSignature called on a detached handle";
    return QUIC_FAILURE;
  }

  if (signature_callback_) {
    QUIC_BUG << "ComputeSignature called while pending";
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

TlsServerHandshaker::SignatureCallback::SignatureCallback(
    TlsServerHandshaker* handshaker)
    : handshaker_(handshaker) {
  QUICHE_DCHECK(!handshaker_->use_proof_source_handle_);
}

void TlsServerHandshaker::SignatureCallback::Run(
    bool ok,
    std::string signature,
    std::unique_ptr<ProofSource::Details> details) {
  if (handshaker_ == nullptr) {
    return;
  }
  if (ok) {
    handshaker_->cert_verify_sig_ = std::move(signature);
    handshaker_->proof_source_details_ = std::move(details);
  }
  int last_expected_ssl_error = handshaker_->expected_ssl_error();
  handshaker_->set_expected_ssl_error(SSL_ERROR_WANT_READ);
  handshaker_->signature_callback_ = nullptr;
  if (last_expected_ssl_error == SSL_ERROR_WANT_PRIVATE_KEY_OPERATION) {
    handshaker_->AdvanceHandshakeFromCallback();
  }
}

void TlsServerHandshaker::SignatureCallback::Cancel() {
  handshaker_ = nullptr;
}

TlsServerHandshaker::DecryptCallback::DecryptCallback(
    TlsServerHandshaker* handshaker)
    : handshaker_(handshaker) {}

void TlsServerHandshaker::DecryptCallback::Run(std::vector<uint8_t> plaintext) {
  if (handshaker_ == nullptr) {
    // The callback was cancelled before we could run.
    return;
  }
  handshaker_->decrypted_session_ticket_ = std::move(plaintext);
  // DecryptCallback::Run could be called synchronously. When that happens, we
  // are currently in the middle of a call to AdvanceHandshake.
  // (AdvanceHandshake called SSL_do_handshake, which through some layers called
  // SessionTicketOpen, which called TicketCrypter::Decrypt, which synchronously
  // called this function.) In that case, the handshake will continue to be
  // processed when this function returns.
  //
  // When this callback is called asynchronously (i.e. the ticket decryption is
  // pending), TlsServerHandshaker is not actively processing handshake
  // messages. We need to have it resume processing handshake messages by
  // calling AdvanceHandshake.
  if (handshaker_->expected_ssl_error() == SSL_ERROR_PENDING_TICKET) {
    handshaker_->AdvanceHandshakeFromCallback();
  }
  // The TicketDecrypter took ownership of this callback when Decrypt was
  // called. Once the callback returns, it will be deleted. Remove the
  // (non-owning) pointer to the callback from the handshaker so the handshaker
  // doesn't have an invalid pointer hanging around.
  handshaker_->ticket_decryption_callback_ = nullptr;
}

void TlsServerHandshaker::DecryptCallback::Cancel() {
  QUICHE_DCHECK(handshaker_);
  handshaker_ = nullptr;
}

TlsServerHandshaker::TlsServerHandshaker(
    QuicSession* session,
    const QuicCryptoServerConfig* crypto_config)
    : TlsHandshaker(this, session),
      QuicCryptoServerStreamBase(session),
      proof_source_(crypto_config->proof_source()),
      pre_shared_key_(crypto_config->pre_shared_key()),
      crypto_negotiated_params_(new QuicCryptoNegotiatedParameters),
      tls_connection_(crypto_config->ssl_ctx(), this),
      crypto_config_(crypto_config) {
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

  if (GetQuicFlag(FLAGS_quic_disable_server_tls_resumption)) {
    SSL_set_options(ssl(), SSL_OP_NO_TICKET);
  }
}

TlsServerHandshaker::~TlsServerHandshaker() {
  CancelOutstandingCallbacks();
}

void TlsServerHandshaker::CancelOutstandingCallbacks() {
  if (use_proof_source_handle_ && proof_source_handle_) {
    proof_source_handle_->CancelPendingOperation();
  }
  if (signature_callback_) {
    signature_callback_->Cancel();
    signature_callback_ = nullptr;
  }
  if (ticket_decryption_callback_) {
    ticket_decryption_callback_->Cancel();
    ticket_decryption_callback_ = nullptr;
  }
}

std::unique_ptr<ProofSourceHandle>
TlsServerHandshaker::MaybeCreateProofSourceHandle() {
  QUICHE_DCHECK(use_proof_source_handle_);
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

bool TlsServerHandshaker::IsZeroRtt() const {
  return SSL_early_data_accepted(ssl());
}

bool TlsServerHandshaker::IsResumption() const {
  return SSL_session_reused(ssl());
}

bool TlsServerHandshaker::ResumptionAttempted() const {
  return ticket_received_;
}

int TlsServerHandshaker::NumServerConfigUpdateMessagesSent() const {
  // SCUP messages aren't supported when using the TLS handshake.
  return 0;
}

const CachedNetworkParameters*
TlsServerHandshaker::PreviousCachedNetworkParams() const {
  return nullptr;
}

void TlsServerHandshaker::SetPreviousCachedNetworkParams(
    CachedNetworkParameters /*cached_network_params*/) {}

void TlsServerHandshaker::OnPacketDecrypted(EncryptionLevel level) {
  if (level == ENCRYPTION_HANDSHAKE && state_ < HANDSHAKE_PROCESSED) {
    state_ = HANDSHAKE_PROCESSED;
    handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_INITIAL);
    handshaker_delegate()->DiscardOldDecryptionKey(ENCRYPTION_INITIAL);
  }
}

void TlsServerHandshaker::OnHandshakeDoneReceived() {
  QUICHE_DCHECK(false);
}

void TlsServerHandshaker::OnNewTokenReceived(absl::string_view /*token*/) {
  QUICHE_DCHECK(false);
}

std::string TlsServerHandshaker::GetAddressToken() const {
  SourceAddressTokens empty_previous_tokens;
  const QuicConnection* connection = session()->connection();
  return crypto_config_->NewSourceAddressToken(
      crypto_config_->source_address_token_boxer(), empty_previous_tokens,
      connection->effective_peer_address().host(),
      connection->random_generator(), connection->clock()->WallNow(),
      /*cached_network_params=*/nullptr);
}

bool TlsServerHandshaker::ValidateAddressToken(absl::string_view token) const {
  SourceAddressTokens tokens;
  HandshakeFailureReason reason = crypto_config_->ParseSourceAddressToken(
      crypto_config_->source_address_token_boxer(), token, &tokens);
  if (reason != HANDSHAKE_OK) {
    QUIC_DLOG(WARNING) << "Failed to parse source address token: "
                       << CryptoUtils::HandshakeFailureReasonToString(reason);
    return false;
  }
  reason = crypto_config_->ValidateSourceAddressTokens(
      tokens, session()->connection()->effective_peer_address().host(),
      session()->connection()->clock()->WallNow(),
      /*cached_network_params=*/nullptr);
  if (reason != HANDSHAKE_OK) {
    QUIC_DLOG(WARNING) << "Failed to validate source address token: "
                       << CryptoUtils::HandshakeFailureReasonToString(reason);
    return false;
  }
  return true;
}

bool TlsServerHandshaker::ShouldSendExpectCTHeader() const {
  return false;
}

const ProofSource::Details* TlsServerHandshaker::ProofSourceDetails() const {
  return proof_source_details_.get();
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

HandshakeState TlsServerHandshaker::GetHandshakeState() const {
  return state_;
}

void TlsServerHandshaker::SetServerApplicationStateForResumption(
    std::unique_ptr<ApplicationState> state) {
  application_state_ = std::move(state);
}

size_t TlsServerHandshaker::BufferSizeLimitForLevel(
    EncryptionLevel level) const {
  return TlsHandshaker::BufferSizeLimitForLevel(level);
}

bool TlsServerHandshaker::KeyUpdateSupportedLocally() const {
  return true;
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
  AdvanceHandshake();
  if (!is_connection_closed()) {
    handshaker_delegate()->OnHandshakeCallbackDone();
  }
}

bool TlsServerHandshaker::ProcessTransportParameters(
    const SSL_CLIENT_HELLO* client_hello,
    std::string* error_details) {
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

  // Chrome clients before 86.0.4233.0 did not send the
  // key_update_not_yet_supported transport parameter, but they did send a
  // Google-internal transport parameter with identifier 0x4751. We treat
  // reception of 0x4751 as having received key_update_not_yet_supported to
  // ensure we do not use key updates with those older clients.
  // TODO(dschinazi) remove this workaround once all of our QUIC+TLS Finch
  // experiments have a min_version greater than 86.0.4233.0.
  if (client_params.custom_parameters.find(
          static_cast<TransportParameters::TransportParameterId>(0x4751)) !=
      client_params.custom_parameters.end()) {
    client_params.key_update_not_yet_supported = true;
  }

  // When interoperating with non-Google implementations that do not send
  // the version extension, set it to what we expect.
  if (client_params.version == 0) {
    client_params.version =
        CreateQuicVersionLabel(session()->connection()->version());
  }

  if (CryptoUtils::ValidateClientHelloVersion(
          client_params.version, session()->connection()->version(),
          session()->supported_versions(), error_details) != QUIC_NO_ERROR ||
      handshaker_delegate()->ProcessTransportParameters(
          client_params, /* is_resumption = */ false, error_details) !=
          QUIC_NO_ERROR) {
    return false;
  }
  ProcessAdditionalTransportParameters(client_params);
  if (!session()->user_agent_id().has_value() &&
      client_params.user_agent_id.has_value()) {
    session()->SetUserAgentId(client_params.user_agent_id.value());
  }

  return true;
}

TlsServerHandshaker::SetTransportParametersResult
TlsServerHandshaker::SetTransportParameters() {
  SetTransportParametersResult result;
  QUICHE_DCHECK(!result.success);

  TransportParameters server_params;
  server_params.perspective = Perspective::IS_SERVER;
  server_params.supported_versions =
      CreateQuicVersionLabelVector(session()->supported_versions());
  server_params.version =
      CreateQuicVersionLabel(session()->connection()->version());

  if (!handshaker_delegate()->FillTransportParameters(&server_params)) {
    return result;
  }

  // Notify QuicConnectionDebugVisitor.
  session()->connection()->OnTransportParametersSent(server_params);

  {  // Ensure |server_params_bytes| is not accessed out of the scope.
    std::vector<uint8_t> server_params_bytes;
    if (!SerializeTransportParameters(session()->connection()->version(),
                                      server_params, &server_params_bytes) ||
        SSL_set_quic_transport_params(ssl(), server_params_bytes.data(),
                                      server_params_bytes.size()) != 1) {
      return result;
    }
    result.quic_transport_params = std::move(server_params_bytes);
  }

  if (application_state_) {
    std::vector<uint8_t> early_data_context;
    if (!SerializeTransportParametersForTicket(
            server_params, *application_state_, &early_data_context)) {
      QUIC_BUG << "Failed to serialize Transport Parameters for ticket.";
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

void TlsServerHandshaker::SetWriteSecret(
    EncryptionLevel level,
    const SSL_CIPHER* cipher,
    const std::vector<uint8_t>& write_secret) {
  if (is_connection_closed()) {
    return;
  }
  if (level == ENCRYPTION_FORWARD_SECURE) {
    encryption_established_ = true;
    // Fill crypto_negotiated_params_:
    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl());
    if (cipher) {
      crypto_negotiated_params_->cipher_suite = SSL_CIPHER_get_value(cipher);
    }
    crypto_negotiated_params_->key_exchange_group = SSL_get_curve_id(ssl());
  }
  TlsHandshaker::SetWriteSecret(level, cipher, write_secret);
}

std::string TlsServerHandshaker::GetAcceptChValueForOrigin(
    const std::string& /*origin*/) const {
  return {};
}

void TlsServerHandshaker::FinishHandshake() {
  if (SSL_in_early_data(ssl())) {
    // If the server accepts early data, SSL_do_handshake returns success twice:
    // once after processing the ClientHello and sending the server's first
    // flight, and then again after the handshake is complete. This results in
    // FinishHandshake getting called twice. On the first call to
    // FinishHandshake, we don't have any confirmation that the client is live,
    // so all end of handshake processing is deferred until the handshake is
    // actually complete.
    QUIC_RESTART_FLAG_COUNT(quic_enable_zero_rtt_for_tls_v2);
    return;
  }
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
    const std::vector<std::string>& /*certs*/,
    std::string* /*error_details*/,
    std::unique_ptr<ProofVerifyDetails>* /*details*/,
    uint8_t* /*out_alert*/,
    std::unique_ptr<ProofVerifierCallback> /*callback*/) {
  QUIC_BUG << "Client certificates are not yet supported on the server";
  return QUIC_FAILURE;
}

void TlsServerHandshaker::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& /*verify_details*/) {}

ssl_private_key_result_t TlsServerHandshaker::PrivateKeySign(
    uint8_t* out,
    size_t* out_len,
    size_t max_out,
    uint16_t sig_alg,
    absl::string_view in) {
  QUICHE_DCHECK_EQ(expected_ssl_error(), SSL_ERROR_WANT_READ);
  if (use_proof_source_handle_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_tls_use_per_handshaker_proof_source, 2,
                                 3);
    QuicAsyncStatus status = proof_source_handle_->ComputeSignature(
        session()->connection()->self_address(),
        session()->connection()->peer_address(), cert_selection_hostname(),
        sig_alg, in, max_out);
    if (status == QUIC_PENDING) {
      set_expected_ssl_error(SSL_ERROR_WANT_PRIVATE_KEY_OPERATION);
    }
    return PrivateKeyComplete(out, out_len, max_out);
  }

  signature_callback_ = new SignatureCallback(this);
  proof_source_->ComputeTlsSignature(
      session()->connection()->self_address(),
      session()->connection()->peer_address(), cert_selection_hostname(),
      sig_alg, in, std::unique_ptr<SignatureCallback>(signature_callback_));
  if (signature_callback_) {
    set_expected_ssl_error(SSL_ERROR_WANT_PRIVATE_KEY_OPERATION);
    return ssl_private_key_retry;
  }
  return PrivateKeyComplete(out, out_len, max_out);
}

ssl_private_key_result_t TlsServerHandshaker::PrivateKeyComplete(
    uint8_t* out,
    size_t* out_len,
    size_t max_out) {
  if (expected_ssl_error() == SSL_ERROR_WANT_PRIVATE_KEY_OPERATION) {
    return ssl_private_key_retry;
  }
  if (!HasValidSignature(max_out)) {
    return ssl_private_key_failure;
  }
  *out_len = cert_verify_sig_.size();
  memcpy(out, cert_verify_sig_.data(), *out_len);
  cert_verify_sig_.clear();
  cert_verify_sig_.shrink_to_fit();
  return ssl_private_key_success;
}

void TlsServerHandshaker::OnComputeSignatureDone(
    bool ok,
    bool is_sync,
    std::string signature,
    std::unique_ptr<ProofSource::Details> details) {
  QUIC_DVLOG(1) << "OnComputeSignatureDone. ok:" << ok
                << ", is_sync:" << is_sync
                << ", len(signature):" << signature.size();
  QUICHE_DCHECK(use_proof_source_handle_);
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

int TlsServerHandshaker::SessionTicketSeal(uint8_t* out,
                                           size_t* out_len,
                                           size_t max_out_len,
                                           absl::string_view in) {
  QUICHE_DCHECK(proof_source_->GetTicketCrypter());
  std::vector<uint8_t> ticket = proof_source_->GetTicketCrypter()->Encrypt(in);
  if (max_out_len < ticket.size()) {
    QUIC_BUG
        << "TicketCrypter returned " << ticket.size()
        << " bytes of ciphertext, which is larger than its max overhead of "
        << max_out_len;
    return 0;  // failure
  }
  *out_len = ticket.size();
  memcpy(out, ticket.data(), ticket.size());
  return 1;  // success
}

ssl_ticket_aead_result_t TlsServerHandshaker::SessionTicketOpen(
    uint8_t* out,
    size_t* out_len,
    size_t max_out_len,
    absl::string_view in) {
  QUICHE_DCHECK(proof_source_->GetTicketCrypter());

  if (!ticket_decryption_callback_) {
    ticket_received_ = true;
    ticket_decryption_callback_ = new DecryptCallback(this);
    proof_source_->GetTicketCrypter()->Decrypt(
        in, std::unique_ptr<DecryptCallback>(ticket_decryption_callback_));
    // Decrypt can run the callback synchronously. In that case, the callback
    // will clear the ticket_decryption_callback_ pointer, and instead of
    // returning ssl_ticket_aead_retry, we should continue processing to return
    // the decrypted ticket.
    //
    // If the callback is not run asynchronously, return ssl_ticket_aead_retry
    // and when the callback is complete this function will be run again to
    // return the result.
    if (ticket_decryption_callback_) {
      set_expected_ssl_error(SSL_ERROR_PENDING_TICKET);
      return ssl_ticket_aead_retry;
    }
  }
  ticket_decryption_callback_ = nullptr;
  set_expected_ssl_error(SSL_ERROR_WANT_READ);
  if (decrypted_session_ticket_.empty()) {
    QUIC_DLOG(ERROR) << "Session ticket decryption failed; ignoring ticket";
    // Ticket decryption failed. Ignore the ticket.
    return ssl_ticket_aead_ignore_ticket;
  }
  if (max_out_len < decrypted_session_ticket_.size()) {
    return ssl_ticket_aead_error;
  }
  memcpy(out, decrypted_session_ticket_.data(),
         decrypted_session_ticket_.size());
  *out_len = decrypted_session_ticket_.size();

  return ssl_ticket_aead_success;
}

ssl_select_cert_result_t TlsServerHandshaker::EarlySelectCertCallback(
    const SSL_CLIENT_HELLO* client_hello) {
  // EarlySelectCertCallback can be called twice from BoringSSL: If the first
  // call returns ssl_select_cert_retry, when cert selection completes,
  // SSL_do_handshake will call it again.
  if (use_proof_source_handle_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_tls_use_per_handshaker_proof_source, 1,
                                 3);
    if (select_cert_status_.has_value()) {
      // This is the second call, return the result directly.
      QUIC_DVLOG(1) << "EarlySelectCertCallback called to continue handshake, "
                       "returning directly. success:"
                    << (select_cert_status_.value() == QUIC_SUCCESS);
      return (select_cert_status_.value() == QUIC_SUCCESS)
                 ? ssl_select_cert_success
                 : ssl_select_cert_error;
    }

    // This is the first call.
    select_cert_status_ = QUIC_PENDING;
    proof_source_handle_ = MaybeCreateProofSourceHandle();
  }

  if (!pre_shared_key_.empty()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    QUIC_BUG << "QUIC server pre-shared keys not yet supported with TLS";
    return ssl_select_cert_error;
  }

  // This callback is called very early by Boring SSL, most of the SSL_get_foo
  // function do not work at this point, but SSL_get_servername does.
  const char* hostname = SSL_get_servername(ssl(), TLSEXT_NAMETYPE_host_name);
  if (hostname) {
    hostname_ = hostname;
    crypto_negotiated_params_->sni =
        QuicHostnameUtils::NormalizeHostname(hostname_);
    if (!ValidateHostname(hostname_)) {
      return ssl_select_cert_error;
    }
    if (hostname_ != crypto_negotiated_params_->sni) {
      QUIC_CODE_COUNT(quic_tls_server_hostname_diff);
      QUIC_LOG_EVERY_N_SEC(WARNING, 300)
          << "Raw and normalized hostnames differ, but both are valid SNIs. "
             "raw hostname:"
          << hostname_ << ", normalized:" << crypto_negotiated_params_->sni;
    } else {
      QUIC_CODE_COUNT(quic_tls_server_hostname_same);
    }
  } else {
    QUIC_LOG(INFO) << "No hostname indicated in SNI";
  }

  if (use_proof_source_handle_) {
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

    const QuicAsyncStatus status = proof_source_handle_->SelectCertificate(
        session()->connection()->self_address(),
        session()->connection()->peer_address(), cert_selection_hostname(),
        absl::string_view(
            reinterpret_cast<const char*>(client_hello->client_hello),
            client_hello->client_hello_len),
        AlpnForVersion(session()->version()),
        set_transport_params_result.quic_transport_params,
        set_transport_params_result.early_data_context);

    QUICHE_DCHECK_EQ(status, select_cert_status().value());

    if (status == QUIC_PENDING) {
      set_expected_ssl_error(SSL_ERROR_PENDING_CERTIFICATE);
      return ssl_select_cert_retry;
    }

    if (status == QUIC_FAILURE) {
      return ssl_select_cert_error;
    }

    return ssl_select_cert_success;
  }

  QuicReferenceCountedPointer<ProofSource::Chain> chain =
      proof_source_->GetCertChain(session()->connection()->self_address(),
                                  session()->connection()->peer_address(),
                                  cert_selection_hostname());
  if (!chain || chain->certs.empty()) {
    QUIC_LOG(ERROR) << "No certs provided for host. raw:" << hostname_
                    << ", normalized:" << crypto_negotiated_params_->sni;
    return ssl_select_cert_error;
  }

  CryptoBuffers cert_buffers = chain->ToCryptoBuffers();
  tls_connection_.SetCertChain(cert_buffers.value);

  std::string error_details;
  if (!ProcessTransportParameters(client_hello, &error_details)) {
    CloseConnection(QUIC_HANDSHAKE_FAILED, error_details);
    return ssl_select_cert_error;
  }
  OverrideQuicConfigDefaults(session()->config());
  session()->OnConfigNegotiated();

  if (!SetTransportParameters().success) {
    QUIC_LOG(ERROR) << "Failed to set transport parameters";
    return ssl_select_cert_error;
  }

  QUIC_DLOG(INFO) << "Set " << chain->certs.size() << " certs for server "
                  << "with hostname " << hostname_;
  return ssl_select_cert_success;
}

void TlsServerHandshaker::OnSelectCertificateDone(
    bool ok,
    bool is_sync,
    const ProofSource::Chain* chain) {
  QUIC_DVLOG(1) << "OnSelectCertificateDone. ok:" << ok
                << ", is_sync:" << is_sync;
  QUICHE_DCHECK(use_proof_source_handle_);

  select_cert_status_ = QUIC_FAILURE;
  if (ok) {
    if (chain && !chain->certs.empty()) {
      tls_connection_.SetCertChain(chain->ToCryptoBuffers().value);
      select_cert_status_ = QUIC_SUCCESS;
    } else {
      QUIC_LOG(ERROR) << "No certs provided for host '" << hostname_ << "'";
    }
  }
  const int last_expected_ssl_error = expected_ssl_error();
  set_expected_ssl_error(SSL_ERROR_WANT_READ);
  if (!is_sync) {
    QUICHE_DCHECK_EQ(last_expected_ssl_error, SSL_ERROR_PENDING_CERTIFICATE);
    AdvanceHandshakeFromCallback();
  }
}

bool TlsServerHandshaker::ValidateHostname(const std::string& hostname) const {
  if (!QuicHostnameUtils::IsValidSNI(hostname)) {
    // TODO(b/151676147): Include this error string in the CONNECTION_CLOSE
    // frame.
    QUIC_LOG(ERROR) << "Invalid SNI provided: \"" << hostname << "\"";
    return false;
  }
  return true;
}

int TlsServerHandshaker::TlsExtServernameCallback(int* /*out_alert*/) {
  // SSL_TLSEXT_ERR_OK causes the server_name extension to be acked in
  // ServerHello.
  return SSL_TLSEXT_ERR_OK;
}

int TlsServerHandshaker::SelectAlpn(const uint8_t** out,
                                    uint8_t* out_len,
                                    const uint8_t* in,
                                    unsigned in_len) {
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

  auto selected_alpn = session()->SelectAlpn(alpns);
  if (selected_alpn == alpns.end()) {
    QUIC_DLOG(ERROR) << "No known ALPN provided by client";
    return SSL_TLSEXT_ERR_NOACK;
  }

  // Enable ALPS for the selected ALPN protocol.
  if (GetQuicReloadableFlag(quic_enable_alps_server)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_enable_alps_server);

    const uint8_t* alps_data = nullptr;
    size_t alps_length = 0;
    std::unique_ptr<char[]> buffer;

    const std::string& origin = crypto_negotiated_params_->sni;
    std::string accept_ch_value = GetAcceptChValueForOrigin(origin);
    if (!accept_ch_value.empty()) {
      AcceptChFrame frame{{{origin, std::move(accept_ch_value)}}};
      alps_length = HttpEncoder::SerializeAcceptChFrame(frame, &buffer);
      alps_data = reinterpret_cast<const uint8_t*>(buffer.get());
    }

    if (SSL_add_application_settings(
            ssl(), reinterpret_cast<const uint8_t*>(selected_alpn->data()),
            selected_alpn->size(), alps_data, alps_length) != 1) {
      QUIC_DLOG(ERROR) << "Failed to enable ALPS";
      return SSL_TLSEXT_ERR_NOACK;
    }
  }

  session()->OnAlpnSelected(*selected_alpn);
  valid_alpn_received_ = true;
  *out_len = selected_alpn->size();
  *out = reinterpret_cast<const uint8_t*>(selected_alpn->data());
  return SSL_TLSEXT_ERR_OK;
}

}  // namespace quic
