// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/tls_server_handshaker.h"

#include <memory>
#include <string>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_hostname_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {

TlsServerHandshaker::SignatureCallback::SignatureCallback(
    TlsServerHandshaker* handshaker)
    : handshaker_(handshaker) {}

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
  DCHECK(handshaker_);
  handshaker_ = nullptr;
}

TlsServerHandshaker::TlsServerHandshaker(
    QuicSession* session,
    const QuicCryptoServerConfig& crypto_config)
    : TlsHandshaker(this, session),
      QuicCryptoServerStreamBase(session),
      proof_source_(crypto_config.proof_source()),
      pre_shared_key_(crypto_config.pre_shared_key()),
      crypto_negotiated_params_(new QuicCryptoNegotiatedParameters),
      tls_connection_(crypto_config.ssl_ctx(), this) {
  DCHECK_EQ(PROTOCOL_TLS1_3,
            session->connection()->version().handshake_protocol);

  // Configure the SSL to be a server.
  SSL_set_accept_state(ssl());

  if (GetQuicFlag(FLAGS_quic_disable_server_tls_resumption)) {
    SSL_set_options(ssl(), SSL_OP_NO_TICKET);
  }
}

TlsServerHandshaker::~TlsServerHandshaker() {
  CancelOutstandingCallbacks();
}

void TlsServerHandshaker::CancelOutstandingCallbacks() {
  if (signature_callback_) {
    signature_callback_->Cancel();
    signature_callback_ = nullptr;
  }
  if (ticket_decryption_callback_) {
    ticket_decryption_callback_->Cancel();
    ticket_decryption_callback_ = nullptr;
  }
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
  DCHECK(false);
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
  if (GetQuicReloadableFlag(
          quic_process_undecryptable_packets_after_async_decrypt_callback) &&
      !is_connection_closed()) {
    QUIC_RELOADABLE_FLAG_COUNT(
        quic_process_undecryptable_packets_after_async_decrypt_callback);
    handshaker_delegate()->OnHandshakeCallbackDone();
  }
}

bool TlsServerHandshaker::ProcessTransportParameters(
    std::string* error_details) {
  TransportParameters client_params;
  const uint8_t* client_params_bytes;
  size_t params_bytes_len;
  SSL_get_peer_quic_transport_params(ssl(), &client_params_bytes,
                                     &params_bytes_len);
  if (params_bytes_len == 0) {
    *error_details = "Client's transport parameters are missing";
    return false;
  }
  std::string parse_error_details;
  if (!ParseTransportParameters(session()->connection()->version(),
                                Perspective::IS_CLIENT, client_params_bytes,
                                params_bytes_len, &client_params,
                                &parse_error_details)) {
    DCHECK(!parse_error_details.empty());
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

bool TlsServerHandshaker::SetTransportParameters() {
  TransportParameters server_params;
  server_params.perspective = Perspective::IS_SERVER;
  server_params.supported_versions =
      CreateQuicVersionLabelVector(session()->supported_versions());
  server_params.version =
      CreateQuicVersionLabel(session()->connection()->version());

  if (!handshaker_delegate()->FillTransportParameters(&server_params)) {
    return false;
  }

  // Notify QuicConnectionDebugVisitor.
  session()->connection()->OnTransportParametersSent(server_params);

  std::vector<uint8_t> server_params_bytes;
  if (!SerializeTransportParameters(session()->connection()->version(),
                                    server_params, &server_params_bytes) ||
      SSL_set_quic_transport_params(ssl(), server_params_bytes.data(),
                                    server_params_bytes.size()) != 1) {
    return false;
  }
  if (application_state_) {
    std::vector<uint8_t> early_data_context;
    if (!SerializeTransportParametersForTicket(
            server_params, *application_state_, &early_data_context)) {
      QUIC_BUG << "Failed to serialize Transport Parameters for ticket.";
      return false;
    }
    SSL_set_quic_early_data_context(ssl(), early_data_context.data(),
                                    early_data_context.size());
    application_state_.reset(nullptr);
  }
  return true;
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
  handshaker_delegate()->DiscardOldDecryptionKey(ENCRYPTION_ZERO_RTT);
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
  signature_callback_ = new SignatureCallback(this);
  proof_source_->ComputeTlsSignature(
      session()->connection()->self_address(),
      session()->connection()->peer_address(), hostname_, sig_alg, in,
      std::unique_ptr<SignatureCallback>(signature_callback_));
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
  if (cert_verify_sig_.size() > max_out || cert_verify_sig_.empty()) {
    return ssl_private_key_failure;
  }
  *out_len = cert_verify_sig_.size();
  memcpy(out, cert_verify_sig_.data(), *out_len);
  cert_verify_sig_.clear();
  cert_verify_sig_.shrink_to_fit();
  return ssl_private_key_success;
}

size_t TlsServerHandshaker::SessionTicketMaxOverhead() {
  DCHECK(proof_source_->GetTicketCrypter());
  return proof_source_->GetTicketCrypter()->MaxOverhead();
}

int TlsServerHandshaker::SessionTicketSeal(uint8_t* out,
                                           size_t* out_len,
                                           size_t max_out_len,
                                           absl::string_view in) {
  DCHECK(proof_source_->GetTicketCrypter());
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
  DCHECK(proof_source_->GetTicketCrypter());

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

int TlsServerHandshaker::SelectCertificate(int* out_alert) {
  const char* hostname = SSL_get_servername(ssl(), TLSEXT_NAMETYPE_host_name);
  if (hostname) {
    hostname_ = hostname;
    crypto_negotiated_params_->sni =
        QuicHostnameUtils::NormalizeHostname(hostname_);
    if (!QuicHostnameUtils::IsValidSNI(hostname_)) {
      // TODO(b/151676147): Include this error string in the CONNECTION_CLOSE
      // frame.
      QUIC_LOG(ERROR) << "Invalid SNI provided: \"" << hostname_ << "\"";
      return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
  } else {
    QUIC_LOG(INFO) << "No hostname indicated in SNI";
  }

  QuicReferenceCountedPointer<ProofSource::Chain> chain =
      proof_source_->GetCertChain(session()->connection()->self_address(),
                                  session()->connection()->peer_address(),
                                  hostname_);
  if (!chain || chain->certs.empty()) {
    QUIC_LOG(ERROR) << "No certs provided for host '" << hostname_ << "'";
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  if (!pre_shared_key_.empty()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    QUIC_BUG << "QUIC server pre-shared keys not yet supported with TLS";
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  CryptoBuffers cert_buffers = chain->ToCryptoBuffers();
  tls_connection_.SetCertChain(cert_buffers.value);

  std::string error_details;
  if (!ProcessTransportParameters(&error_details)) {
    CloseConnection(QUIC_HANDSHAKE_FAILED, error_details);
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
  OverrideQuicConfigDefaults(session()->config());
  session()->OnConfigNegotiated();

  if (!SetTransportParameters()) {
    QUIC_LOG(ERROR) << "Failed to set transport parameters";
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  QUIC_DLOG(INFO) << "Set " << chain->certs.size() << " certs for server "
                  << "with hostname " << hostname_;
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

  session()->OnAlpnSelected(*selected_alpn);
  valid_alpn_received_ = true;
  *out_len = selected_alpn->size();
  *out = reinterpret_cast<const uint8_t*>(selected_alpn->data());
  return SSL_TLSEXT_ERR_OK;
}

}  // namespace quic
