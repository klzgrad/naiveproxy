// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/tls_client_handshaker.h"

#include <cstring>
#include <string>

#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_client_config.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_hostname_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {

TlsClientHandshaker::ProofVerifierCallbackImpl::ProofVerifierCallbackImpl(
    TlsClientHandshaker* parent)
    : parent_(parent) {}

TlsClientHandshaker::ProofVerifierCallbackImpl::~ProofVerifierCallbackImpl() {}

void TlsClientHandshaker::ProofVerifierCallbackImpl::Run(
    bool ok,
    const std::string& /*error_details*/,
    std::unique_ptr<ProofVerifyDetails>* details) {
  if (parent_ == nullptr) {
    return;
  }

  parent_->verify_details_ = std::move(*details);
  parent_->verify_result_ = ok ? ssl_verify_ok : ssl_verify_invalid;
  parent_->state_ = STATE_HANDSHAKE_RUNNING;
  parent_->proof_verify_callback_ = nullptr;
  if (parent_->verify_details_) {
    parent_->proof_handler_->OnProofVerifyDetailsAvailable(
        *parent_->verify_details_);
  }
  parent_->AdvanceHandshake();
}

void TlsClientHandshaker::ProofVerifierCallbackImpl::Cancel() {
  parent_ = nullptr;
}

TlsClientHandshaker::TlsClientHandshaker(
    const QuicServerId& server_id,
    QuicCryptoStream* stream,
    QuicSession* session,
    std::unique_ptr<ProofVerifyContext> verify_context,
    QuicCryptoClientConfig* crypto_config,
    QuicCryptoClientStream::ProofHandler* proof_handler,
    bool has_application_state)
    : TlsHandshaker(stream, session),
      session_(session),
      server_id_(server_id),
      proof_verifier_(crypto_config->proof_verifier()),
      verify_context_(std::move(verify_context)),
      proof_handler_(proof_handler),
      session_cache_(crypto_config->session_cache()),
      user_agent_id_(crypto_config->user_agent_id()),
      pre_shared_key_(crypto_config->pre_shared_key()),
      crypto_negotiated_params_(new QuicCryptoNegotiatedParameters),
      has_application_state_(has_application_state),
      tls_connection_(crypto_config->ssl_ctx(), this) {}

TlsClientHandshaker::~TlsClientHandshaker() {
  if (proof_verify_callback_) {
    proof_verify_callback_->Cancel();
  }
}

bool TlsClientHandshaker::CryptoConnect() {
  state_ = STATE_HANDSHAKE_RUNNING;

  if (!pre_shared_key_.empty()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    std::string error_details =
        "QUIC client pre-shared keys not yet supported with TLS";
    QUIC_BUG << error_details;
    CloseConnection(QUIC_HANDSHAKE_FAILED, error_details);
    return false;
  }

  // Set the SNI to send, if any.
  SSL_set_connect_state(ssl());
  if (QUIC_DLOG_INFO_IS_ON() &&
      !QuicHostnameUtils::IsValidSNI(server_id_.host())) {
    QUIC_DLOG(INFO) << "Client configured with invalid hostname \""
                    << server_id_.host() << "\", not sending as SNI";
  }
  if (!server_id_.host().empty() &&
      (QuicHostnameUtils::IsValidSNI(server_id_.host()) ||
       allow_invalid_sni_for_tests_) &&
      SSL_set_tlsext_host_name(ssl(), server_id_.host().c_str()) != 1) {
    return false;
  }

  if (!SetAlpn()) {
    CloseConnection(QUIC_HANDSHAKE_FAILED, "Client failed to set ALPN");
    return false;
  }

  // Set the Transport Parameters to send in the ClientHello
  if (!SetTransportParameters()) {
    CloseConnection(QUIC_HANDSHAKE_FAILED,
                    "Client failed to set Transport Parameters");
    return false;
  }

  // Set a session to resume, if there is one.
  if (session_cache_) {
    cached_state_ = session_cache_->Lookup(server_id_, SSL_get_SSL_CTX(ssl()));
  }
  if (cached_state_) {
    SSL_set_session(ssl(), cached_state_->tls_session.get());
  }

  // Start the handshake.
  AdvanceHandshake();
  return session()->connection()->connected();
}

bool TlsClientHandshaker::PrepareZeroRttConfig(
    QuicResumptionState* cached_state) {
  std::string error_details;
  if (!cached_state->transport_params ||
      handshaker_delegate()->ProcessTransportParameters(
          *(cached_state->transport_params),
          /*is_resumption = */ true, &error_details) != QUIC_NO_ERROR) {
    QUIC_BUG << "Unable to parse cached transport parameters.";
    CloseConnection(QUIC_HANDSHAKE_FAILED,
                    "Client failed to parse cached Transport Parameters.");
    return false;
  }

  session()->connection()->OnTransportParametersResumed(
      *(cached_state->transport_params));
  session()->OnConfigNegotiated();

  if (has_application_state_) {
    if (!cached_state->application_state ||
        !session()->ResumeApplicationState(
            cached_state->application_state.get())) {
      QUIC_BUG << "Unable to parse cached application state.";
      CloseConnection(QUIC_HANDSHAKE_FAILED,
                      "Client failed to parse cached application state.");
      return false;
    }
  }
  return true;
}

static bool IsValidAlpn(const std::string& alpn_string) {
  return alpn_string.length() <= std::numeric_limits<uint8_t>::max();
}

bool TlsClientHandshaker::SetAlpn() {
  std::vector<std::string> alpns = session()->GetAlpnsToOffer();
  if (alpns.empty()) {
    if (allow_empty_alpn_for_tests_) {
      return true;
    }

    QUIC_BUG << "ALPN missing";
    return false;
  }
  if (!std::all_of(alpns.begin(), alpns.end(), IsValidAlpn)) {
    QUIC_BUG << "ALPN too long";
    return false;
  }

  // SSL_set_alpn_protos expects a sequence of one-byte-length-prefixed
  // strings.
  uint8_t alpn[1024];
  QuicDataWriter alpn_writer(sizeof(alpn), reinterpret_cast<char*>(alpn));
  bool success = true;
  for (const std::string& alpn_string : alpns) {
    success = success && alpn_writer.WriteUInt8(alpn_string.size()) &&
              alpn_writer.WriteStringPiece(alpn_string);
  }
  success =
      success && (SSL_set_alpn_protos(ssl(), alpn, alpn_writer.length()) == 0);
  if (!success) {
    QUIC_BUG << "Failed to set ALPN: "
             << quiche::QuicheTextUtils::HexDump(quiche::QuicheStringPiece(
                    alpn_writer.data(), alpn_writer.length()));
    return false;
  }
  QUIC_DLOG(INFO) << "Client using ALPN: '" << alpns[0] << "'";
  return true;
}

bool TlsClientHandshaker::SetTransportParameters() {
  TransportParameters params;
  params.perspective = Perspective::IS_CLIENT;
  params.version =
      CreateQuicVersionLabel(session()->supported_versions().front());

  if (!handshaker_delegate()->FillTransportParameters(&params)) {
    return false;
  }
  if (!user_agent_id_.empty()) {
    params.user_agent_id = user_agent_id_;
  }

  // Notify QuicConnectionDebugVisitor.
  session()->connection()->OnTransportParametersSent(params);

  std::vector<uint8_t> param_bytes;
  return SerializeTransportParameters(session()->connection()->version(),
                                      params, &param_bytes) &&
         SSL_set_quic_transport_params(ssl(), param_bytes.data(),
                                       param_bytes.size()) == 1;
}

bool TlsClientHandshaker::ProcessTransportParameters(
    std::string* error_details) {
  received_transport_params_ = std::make_unique<TransportParameters>();
  const uint8_t* param_bytes;
  size_t param_bytes_len;
  SSL_get_peer_quic_transport_params(ssl(), &param_bytes, &param_bytes_len);
  if (param_bytes_len == 0) {
    *error_details = "Server's transport parameters are missing";
    return false;
  }
  std::string parse_error_details;
  if (!ParseTransportParameters(
          session()->connection()->version(), Perspective::IS_SERVER,
          param_bytes, param_bytes_len, received_transport_params_.get(),
          &parse_error_details)) {
    DCHECK(!parse_error_details.empty());
    *error_details =
        "Unable to parse server's transport parameters: " + parse_error_details;
    return false;
  }

  // Notify QuicConnectionDebugVisitor.
  session()->connection()->OnTransportParametersReceived(
      *received_transport_params_);

  // When interoperating with non-Google implementations that do not send
  // the version extension, set it to what we expect.
  if (received_transport_params_->version == 0) {
    received_transport_params_->version =
        CreateQuicVersionLabel(session()->connection()->version());
  }
  if (received_transport_params_->supported_versions.empty()) {
    received_transport_params_->supported_versions.push_back(
        received_transport_params_->version);
  }

  if (received_transport_params_->version !=
      CreateQuicVersionLabel(session()->connection()->version())) {
    *error_details = "Version mismatch detected";
    return false;
  }
  if (CryptoUtils::ValidateServerHelloVersions(
          received_transport_params_->supported_versions,
          session()->connection()->server_supported_versions(),
          error_details) != QUIC_NO_ERROR ||
      handshaker_delegate()->ProcessTransportParameters(
          *received_transport_params_, /* is_resumption = */ false,
          error_details) != QUIC_NO_ERROR) {
    DCHECK(!error_details->empty());
    return false;
  }

  session()->OnConfigNegotiated();
  if (state_ == STATE_CONNECTION_CLOSED) {
    *error_details =
        "Session closed the connection when parsing negotiated config.";
    return false;
  }
  return true;
}

int TlsClientHandshaker::num_sent_client_hellos() const {
  return 0;
}

bool TlsClientHandshaker::IsResumption() const {
  QUIC_BUG_IF(!one_rtt_keys_available_);
  return SSL_session_reused(ssl()) == 1;
}

bool TlsClientHandshaker::EarlyDataAccepted() const {
  QUIC_BUG_IF(!one_rtt_keys_available_);
  return SSL_early_data_accepted(ssl()) == 1;
}

ssl_early_data_reason_t TlsClientHandshaker::EarlyDataReason() const {
  return TlsHandshaker::EarlyDataReason();
}

bool TlsClientHandshaker::ReceivedInchoateReject() const {
  QUIC_BUG_IF(!one_rtt_keys_available_);
  // REJ messages are a QUIC crypto feature, so TLS always returns false.
  return false;
}

int TlsClientHandshaker::num_scup_messages_received() const {
  // SCUP messages aren't sent or received when using the TLS handshake.
  return 0;
}

std::string TlsClientHandshaker::chlo_hash() const {
  return "";
}

bool TlsClientHandshaker::encryption_established() const {
  return encryption_established_;
}

bool TlsClientHandshaker::one_rtt_keys_available() const {
  return one_rtt_keys_available_;
}

const QuicCryptoNegotiatedParameters&
TlsClientHandshaker::crypto_negotiated_params() const {
  return *crypto_negotiated_params_;
}

CryptoMessageParser* TlsClientHandshaker::crypto_message_parser() {
  return TlsHandshaker::crypto_message_parser();
}

HandshakeState TlsClientHandshaker::GetHandshakeState() const {
  if (handshake_confirmed_) {
    return HANDSHAKE_CONFIRMED;
  }
  if (one_rtt_keys_available_) {
    return HANDSHAKE_COMPLETE;
  }
  if (state_ >= STATE_ENCRYPTION_HANDSHAKE_DATA_SENT) {
    return HANDSHAKE_PROCESSED;
  }
  return HANDSHAKE_START;
}

size_t TlsClientHandshaker::BufferSizeLimitForLevel(
    EncryptionLevel level) const {
  return TlsHandshaker::BufferSizeLimitForLevel(level);
}

void TlsClientHandshaker::OnOneRttPacketAcknowledged() {
  OnHandshakeConfirmed();
}

void TlsClientHandshaker::OnHandshakePacketSent() {
  if (initial_keys_dropped_) {
    return;
  }
  initial_keys_dropped_ = true;
  handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_INITIAL);
  handshaker_delegate()->DiscardOldDecryptionKey(ENCRYPTION_INITIAL);
}

void TlsClientHandshaker::OnConnectionClosed(QuicErrorCode /*error*/,
                                             ConnectionCloseSource /*source*/) {
  state_ = STATE_CONNECTION_CLOSED;
}

void TlsClientHandshaker::OnHandshakeDoneReceived() {
  if (!one_rtt_keys_available_) {
    CloseConnection(QUIC_HANDSHAKE_FAILED,
                    "Unexpected handshake done received");
    return;
  }
  OnHandshakeConfirmed();
}

void TlsClientHandshaker::SetWriteSecret(
    EncryptionLevel level,
    const SSL_CIPHER* cipher,
    const std::vector<uint8_t>& write_secret) {
  if (state_ == STATE_CONNECTION_CLOSED) {
    return;
  }
  if (level == ENCRYPTION_FORWARD_SECURE || level == ENCRYPTION_ZERO_RTT) {
    encryption_established_ = true;
  }
  const bool postpone_discarding_zero_rtt_keys =
      GetQuicReloadableFlag(quic_postpone_discarding_zero_rtt_keys);
  if (!postpone_discarding_zero_rtt_keys &&
      level == ENCRYPTION_FORWARD_SECURE) {
    handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_ZERO_RTT);
  }
  TlsHandshaker::SetWriteSecret(level, cipher, write_secret);
  if (postpone_discarding_zero_rtt_keys && level == ENCRYPTION_FORWARD_SECURE) {
    handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_ZERO_RTT);
  }
}

void TlsClientHandshaker::OnHandshakeConfirmed() {
  DCHECK(one_rtt_keys_available_);
  if (handshake_confirmed_) {
    return;
  }
  handshake_confirmed_ = true;
  handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_HANDSHAKE);
  handshaker_delegate()->DiscardOldDecryptionKey(ENCRYPTION_HANDSHAKE);
}

void TlsClientHandshaker::AdvanceHandshake() {
  if (state_ == STATE_CONNECTION_CLOSED) {
    QUIC_LOG(INFO)
        << "TlsClientHandshaker received message after connection closed";
    return;
  }
  if (state_ == STATE_IDLE) {
    CloseConnection(QUIC_HANDSHAKE_FAILED,
                    "Client observed TLS handshake idle failure");
    return;
  }
  if (state_ == STATE_HANDSHAKE_COMPLETE) {
    int rv = SSL_process_quic_post_handshake(ssl());
    if (rv != 1) {
      CloseConnection(QUIC_HANDSHAKE_FAILED, "Unexpected post-handshake data");
    }
    return;
  }

  QUIC_LOG(INFO) << "TlsClientHandshaker: continuing handshake";
  int rv = SSL_do_handshake(ssl());
  if (rv == 1) {
    FinishHandshake();
    return;
  }
  int ssl_error = SSL_get_error(ssl(), rv);
  bool should_close = true;
  if (ssl_error == SSL_ERROR_EARLY_DATA_REJECTED) {
    HandleZeroRttReject();
    return;
  }
  switch (state_) {
    case STATE_HANDSHAKE_RUNNING:
      should_close = ssl_error != SSL_ERROR_WANT_READ;
      break;
    case STATE_CERT_VERIFY_PENDING:
      should_close = ssl_error != SSL_ERROR_WANT_CERTIFICATE_VERIFY;
      break;
    default:
      should_close = true;
  }
  if (should_close && state_ != STATE_CONNECTION_CLOSED) {
    // TODO(nharper): Surface error details from the error queue when ssl_error
    // is SSL_ERROR_SSL.
    QUIC_LOG(WARNING) << "SSL_do_handshake failed; closing connection";
    CloseConnection(QUIC_HANDSHAKE_FAILED,
                    "Client observed TLS handshake failure");
  }
}

void TlsClientHandshaker::CloseConnection(QuicErrorCode error,
                                          const std::string& reason_phrase) {
  DCHECK(!reason_phrase.empty());
  state_ = STATE_CONNECTION_CLOSED;
  stream()->OnUnrecoverableError(error, reason_phrase);
}

void TlsClientHandshaker::FinishHandshake() {
  // Fill crypto_negotiated_params_:
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl());
  if (cipher) {
    crypto_negotiated_params_->cipher_suite = SSL_CIPHER_get_value(cipher);
  }
  crypto_negotiated_params_->key_exchange_group = SSL_get_curve_id(ssl());
  crypto_negotiated_params_->peer_signature_algorithm =
      SSL_get_peer_signature_algorithm(ssl());
  if (SSL_in_early_data(ssl())) {
    // SSL_do_handshake returns after sending the ClientHello if the session is
    // 0-RTT-capable, which means that FinishHandshake will get called twice -
    // the first time after sending the ClientHello, and the second time after
    // the handshake is complete. If we're in the first time FinishHandshake is
    // called, we can't do any end-of-handshake processing.

    // If we're attempting a 0-RTT handshake, then we need to let the transport
    // and application know what state to apply to early data.
    PrepareZeroRttConfig(cached_state_.get());
    return;
  }
  QUIC_LOG(INFO) << "Client: handshake finished";
  state_ = STATE_HANDSHAKE_COMPLETE;

  std::string error_details;
  if (!ProcessTransportParameters(&error_details)) {
    DCHECK(!error_details.empty());
    CloseConnection(QUIC_HANDSHAKE_FAILED, error_details);
    return;
  }

  const uint8_t* alpn_data = nullptr;
  unsigned alpn_length = 0;
  SSL_get0_alpn_selected(ssl(), &alpn_data, &alpn_length);

  if (alpn_length == 0) {
    QUIC_DLOG(ERROR) << "Client: server did not select ALPN";
    // TODO(b/130164908) this should send no_application_protocol
    // instead of QUIC_HANDSHAKE_FAILED.
    CloseConnection(QUIC_HANDSHAKE_FAILED, "Server did not select ALPN");
    return;
  }

  std::string received_alpn_string(reinterpret_cast<const char*>(alpn_data),
                                   alpn_length);
  std::vector<std::string> offered_alpns = session()->GetAlpnsToOffer();
  if (std::find(offered_alpns.begin(), offered_alpns.end(),
                received_alpn_string) == offered_alpns.end()) {
    QUIC_LOG(ERROR) << "Client: received mismatched ALPN '"
                    << received_alpn_string;
    // TODO(b/130164908) this should send no_application_protocol
    // instead of QUIC_HANDSHAKE_FAILED.
    CloseConnection(QUIC_HANDSHAKE_FAILED, "Client received mismatched ALPN");
    return;
  }
  session()->OnAlpnSelected(received_alpn_string);
  QUIC_DLOG(INFO) << "Client: server selected ALPN: '" << received_alpn_string
                  << "'";
  one_rtt_keys_available_ = true;
  handshaker_delegate()->OnTlsHandshakeComplete();
}

void TlsClientHandshaker::HandleZeroRttReject() {
  QUIC_LOG(INFO) << "0-RTT handshake attempted but was rejected by the server";
  DCHECK(session_cache_);
  // Disable encrytion to block outgoing data until 1-RTT keys are available.
  encryption_established_ = false;
  handshaker_delegate()->OnZeroRttRejected(EarlyDataReason());
  SSL_reset_early_data_reject(ssl());
  session_cache_->ClearEarlyData(server_id_);
  AdvanceHandshake();
}

enum ssl_verify_result_t TlsClientHandshaker::VerifyCert(uint8_t* out_alert) {
  if (verify_result_ != ssl_verify_retry ||
      state_ == STATE_CERT_VERIFY_PENDING) {
    enum ssl_verify_result_t result = verify_result_;
    verify_result_ = ssl_verify_retry;
    return result;
  }
  const STACK_OF(CRYPTO_BUFFER)* cert_chain = SSL_get0_peer_certificates(ssl());
  if (cert_chain == nullptr) {
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }
  // TODO(nharper): Pass the CRYPTO_BUFFERs into the QUIC stack to avoid copies.
  std::vector<std::string> certs;
  for (CRYPTO_BUFFER* cert : cert_chain) {
    certs.push_back(
        std::string(reinterpret_cast<const char*>(CRYPTO_BUFFER_data(cert)),
                    CRYPTO_BUFFER_len(cert)));
  }
  const uint8_t* ocsp_response_raw;
  size_t ocsp_response_len;
  SSL_get0_ocsp_response(ssl(), &ocsp_response_raw, &ocsp_response_len);
  std::string ocsp_response(reinterpret_cast<const char*>(ocsp_response_raw),
                            ocsp_response_len);
  const uint8_t* sct_list_raw;
  size_t sct_list_len;
  SSL_get0_signed_cert_timestamp_list(ssl(), &sct_list_raw, &sct_list_len);
  std::string sct_list(reinterpret_cast<const char*>(sct_list_raw),
                       sct_list_len);

  ProofVerifierCallbackImpl* proof_verify_callback =
      new ProofVerifierCallbackImpl(this);

  QuicAsyncStatus verify_result = proof_verifier_->VerifyCertChain(
      server_id_.host(), server_id_.port(), certs, ocsp_response, sct_list,
      verify_context_.get(), &cert_verify_error_details_, &verify_details_,
      std::unique_ptr<ProofVerifierCallback>(proof_verify_callback));
  switch (verify_result) {
    case QUIC_SUCCESS:
      if (verify_details_) {
        proof_handler_->OnProofVerifyDetailsAvailable(*verify_details_);
      }
      return ssl_verify_ok;
    case QUIC_PENDING:
      proof_verify_callback_ = proof_verify_callback;
      state_ = STATE_CERT_VERIFY_PENDING;
      return ssl_verify_retry;
    case QUIC_FAILURE:
    default:
      QUIC_LOG(INFO) << "Cert chain verification failed: "
                     << cert_verify_error_details_;
      return ssl_verify_invalid;
  }
}

void TlsClientHandshaker::InsertSession(bssl::UniquePtr<SSL_SESSION> session) {
  if (!received_transport_params_) {
    QUIC_BUG << "Transport parameters isn't received";
    return;
  }
  if (session_cache_ == nullptr) {
    QUIC_DVLOG(1) << "No session cache, not inserting a session";
    return;
  }
  if (has_application_state_ && !received_application_state_) {
    // Application state is not received yet. cache the sessions.
    if (cached_tls_sessions_[0] != nullptr) {
      cached_tls_sessions_[1] = std::move(cached_tls_sessions_[0]);
    }
    cached_tls_sessions_[0] = std::move(session);
    return;
  }
  session_cache_->Insert(server_id_, std::move(session),
                         *received_transport_params_,
                         received_application_state_.get());
}

void TlsClientHandshaker::WriteMessage(EncryptionLevel level,
                                       quiche::QuicheStringPiece data) {
  if (level == ENCRYPTION_HANDSHAKE &&
      state_ < STATE_ENCRYPTION_HANDSHAKE_DATA_SENT) {
    state_ = STATE_ENCRYPTION_HANDSHAKE_DATA_SENT;
  }
  TlsHandshaker::WriteMessage(level, data);
}

void TlsClientHandshaker::SetServerApplicationStateForResumption(
    std::unique_ptr<ApplicationState> application_state) {
  DCHECK_EQ(STATE_HANDSHAKE_COMPLETE, state_);
  received_application_state_ = std::move(application_state);
  // At least one tls session is cached before application state is received. So
  // insert now.
  if (session_cache_ != nullptr && cached_tls_sessions_[0] != nullptr) {
    if (cached_tls_sessions_[1] != nullptr) {
      // Insert the older session first.
      session_cache_->Insert(server_id_, std::move(cached_tls_sessions_[1]),
                             *received_transport_params_,
                             received_application_state_.get());
    }
    session_cache_->Insert(server_id_, std::move(cached_tls_sessions_[0]),
                           *received_transport_params_,
                           received_application_state_.get());
  }
}

}  // namespace quic
