// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/tls_client_handshaker.h"

#include <cstring>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/crypto/transport_parameters.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_hostname_utils.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

TlsClientHandshaker::TlsClientHandshaker(
    const QuicServerId& server_id, QuicCryptoStream* stream,
    QuicSession* session, std::unique_ptr<ProofVerifyContext> verify_context,
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
      tls_connection_(crypto_config->ssl_ctx(), this, session->GetSSLConfig()) {
  if (crypto_config->tls_signature_algorithms().has_value()) {
    SSL_set1_sigalgs_list(ssl(),
                          crypto_config->tls_signature_algorithms()->c_str());
  }
  if (crypto_config->proof_source() != nullptr) {
    const ClientProofSource::CertAndKey* cert_and_key =
        crypto_config->proof_source()->GetCertAndKey(server_id.host());
    if (cert_and_key != nullptr) {
      QUIC_DVLOG(1) << "Setting client cert and key for " << server_id.host();
      tls_connection_.SetCertChain(cert_and_key->chain->ToCryptoBuffers().value,
                                   cert_and_key->private_key.private_key());
    }
  }
#if BORINGSSL_API_VERSION >= 22
  if (!crypto_config->preferred_groups().empty()) {
    SSL_set1_group_ids(ssl(), crypto_config->preferred_groups().data(),
                       crypto_config->preferred_groups().size());
  }
#endif  // BORINGSSL_API_VERSION
}

TlsClientHandshaker::~TlsClientHandshaker() {}

bool TlsClientHandshaker::CryptoConnect() {
  if (!pre_shared_key_.empty()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    std::string error_details =
        "QUIC client pre-shared keys not yet supported with TLS";
    QUIC_BUG(quic_bug_10576_1) << error_details;
    CloseConnection(QUIC_HANDSHAKE_FAILED, error_details);
    return false;
  }

  // Make sure we use the right TLS extension codepoint.
  int use_legacy_extension = 0;
  if (session()->version().UsesLegacyTlsExtension()) {
    use_legacy_extension = 1;
  }
  SSL_set_quic_use_legacy_codepoint(ssl(), use_legacy_extension);

  // TODO(b/193650832) Add SetFromConfig to QUIC handshakers and remove reliance
  // on session pointer.
#if BORINGSSL_API_VERSION >= 16
  // Ask BoringSSL to randomize the order of TLS extensions.
  SSL_set_permute_extensions(ssl(), true);
#endif  // BORINGSSL_API_VERSION

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
    cached_state_ = session_cache_->Lookup(
        server_id_, session()->GetClock()->WallNow(), SSL_get_SSL_CTX(ssl()));
  }
  if (cached_state_) {
    SSL_set_session(ssl(), cached_state_->tls_session.get());
    if (!cached_state_->token.empty()) {
      session()->SetSourceAddressTokenToSend(cached_state_->token);
    }
  }

  SSL_set_enable_ech_grease(ssl(),
                            tls_connection_.ssl_config().ech_grease_enabled);
  if (!tls_connection_.ssl_config().ech_config_list.empty() &&
      !SSL_set1_ech_config_list(
          ssl(),
          reinterpret_cast<const uint8_t*>(
              tls_connection_.ssl_config().ech_config_list.data()),
          tls_connection_.ssl_config().ech_config_list.size())) {
    CloseConnection(QUIC_HANDSHAKE_FAILED,
                    "Client failed to set ECHConfigList");
    return false;
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
    QUIC_BUG(quic_bug_10576_2)
        << "Unable to parse cached transport parameters.";
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
      QUIC_BUG(quic_bug_10576_3) << "Unable to parse cached application state.";
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

    QUIC_BUG(quic_bug_10576_4) << "ALPN missing";
    return false;
  }
  if (!std::all_of(alpns.begin(), alpns.end(), IsValidAlpn)) {
    QUIC_BUG(quic_bug_10576_5) << "ALPN too long";
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
    QUIC_BUG(quic_bug_10576_6)
        << "Failed to set ALPN: "
        << quiche::QuicheTextUtils::HexDump(
               absl::string_view(alpn_writer.data(), alpn_writer.length()));
    return false;
  }

  // Enable ALPS only for versions that use HTTP/3 frames.
  for (const std::string& alpn_string : alpns) {
    for (const ParsedQuicVersion& version : session()->supported_versions()) {
      if (!version.UsesHttp3() || AlpnForVersion(version) != alpn_string) {
        continue;
      }
      if (SSL_add_application_settings(
              ssl(), reinterpret_cast<const uint8_t*>(alpn_string.data()),
              alpn_string.size(), nullptr, /* settings_len = */ 0) != 1) {
        QUIC_BUG(quic_bug_10576_7) << "Failed to enable ALPS.";
        return false;
      }
      break;
    }
  }

  QUIC_DLOG(INFO) << "Client using ALPN: '" << alpns[0] << "'";
  return true;
}

bool TlsClientHandshaker::SetTransportParameters() {
  TransportParameters params;
  params.perspective = Perspective::IS_CLIENT;
  params.legacy_version_information =
      TransportParameters::LegacyVersionInformation();
  params.legacy_version_information->version =
      CreateQuicVersionLabel(session()->supported_versions().front());
  params.version_information = TransportParameters::VersionInformation();
  const QuicVersionLabel version = CreateQuicVersionLabel(session()->version());
  params.version_information->chosen_version = version;
  params.version_information->other_versions.push_back(version);

  if (!handshaker_delegate()->FillTransportParameters(&params)) {
    return false;
  }

  // Notify QuicConnectionDebugVisitor.
  session()->connection()->OnTransportParametersSent(params);

  std::vector<uint8_t> param_bytes;
  return SerializeTransportParameters(params, &param_bytes) &&
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
    QUICHE_DCHECK(!parse_error_details.empty());
    *error_details =
        "Unable to parse server's transport parameters: " + parse_error_details;
    return false;
  }

  // Notify QuicConnectionDebugVisitor.
  session()->connection()->OnTransportParametersReceived(
      *received_transport_params_);

  if (received_transport_params_->legacy_version_information.has_value()) {
    if (received_transport_params_->legacy_version_information->version !=
        CreateQuicVersionLabel(session()->connection()->version())) {
      *error_details = "Version mismatch detected";
      return false;
    }
    if (CryptoUtils::ValidateServerHelloVersions(
            received_transport_params_->legacy_version_information
                ->supported_versions,
            session()->connection()->server_supported_versions(),
            error_details) != QUIC_NO_ERROR) {
      QUICHE_DCHECK(!error_details->empty());
      return false;
    }
  }
  if (received_transport_params_->version_information.has_value()) {
    if (!CryptoUtils::ValidateChosenVersion(
            received_transport_params_->version_information->chosen_version,
            session()->version(), error_details)) {
      QUICHE_DCHECK(!error_details->empty());
      return false;
    }
    if (!CryptoUtils::CryptoUtils::ValidateServerVersions(
            received_transport_params_->version_information->other_versions,
            session()->version(),
            session()->client_original_supported_versions(), error_details)) {
      QUICHE_DCHECK(!error_details->empty());
      return false;
    }
  }

  if (handshaker_delegate()->ProcessTransportParameters(
          *received_transport_params_, /* is_resumption = */ false,
          error_details) != QUIC_NO_ERROR) {
    QUICHE_DCHECK(!error_details->empty());
    return false;
  }

  session()->OnConfigNegotiated();
  if (is_connection_closed()) {
    *error_details =
        "Session closed the connection when parsing negotiated config.";
    return false;
  }
  return true;
}

int TlsClientHandshaker::num_sent_client_hellos() const { return 0; }

bool TlsClientHandshaker::ResumptionAttempted() const {
  QUIC_BUG_IF(quic_tls_client_resumption_attempted, !encryption_established_);
  return cached_state_ != nullptr;
}

bool TlsClientHandshaker::IsResumption() const {
  QUIC_BUG_IF(quic_bug_12736_1, !one_rtt_keys_available());
  return SSL_session_reused(ssl()) == 1;
}

bool TlsClientHandshaker::EarlyDataAccepted() const {
  QUIC_BUG_IF(quic_bug_12736_2, !one_rtt_keys_available());
  return SSL_early_data_accepted(ssl()) == 1;
}

ssl_early_data_reason_t TlsClientHandshaker::EarlyDataReason() const {
  return TlsHandshaker::EarlyDataReason();
}

bool TlsClientHandshaker::ReceivedInchoateReject() const {
  QUIC_BUG_IF(quic_bug_12736_3, !one_rtt_keys_available());
  // REJ messages are a QUIC crypto feature, so TLS always returns false.
  return false;
}

int TlsClientHandshaker::num_scup_messages_received() const {
  // SCUP messages aren't sent or received when using the TLS handshake.
  return 0;
}

std::string TlsClientHandshaker::chlo_hash() const { return ""; }

bool TlsClientHandshaker::ExportKeyingMaterial(absl::string_view label,
                                               absl::string_view context,
                                               size_t result_len,
                                               std::string* result) {
  return ExportKeyingMaterialForLabel(label, context, result_len, result);
}

bool TlsClientHandshaker::encryption_established() const {
  return encryption_established_;
}

bool TlsClientHandshaker::IsCryptoFrameExpectedForEncryptionLevel(
    EncryptionLevel level) const {
  return level != ENCRYPTION_ZERO_RTT;
}

EncryptionLevel TlsClientHandshaker::GetEncryptionLevelToSendCryptoDataOfSpace(
    PacketNumberSpace space) const {
  switch (space) {
    case INITIAL_DATA:
      return ENCRYPTION_INITIAL;
    case HANDSHAKE_DATA:
      return ENCRYPTION_HANDSHAKE;
    default:
      QUICHE_DCHECK(false);
      return NUM_ENCRYPTION_LEVELS;
  }
}

bool TlsClientHandshaker::one_rtt_keys_available() const {
  return state_ >= HANDSHAKE_COMPLETE;
}

const QuicCryptoNegotiatedParameters&
TlsClientHandshaker::crypto_negotiated_params() const {
  return *crypto_negotiated_params_;
}

CryptoMessageParser* TlsClientHandshaker::crypto_message_parser() {
  return TlsHandshaker::crypto_message_parser();
}

HandshakeState TlsClientHandshaker::GetHandshakeState() const { return state_; }

size_t TlsClientHandshaker::BufferSizeLimitForLevel(
    EncryptionLevel level) const {
  return TlsHandshaker::BufferSizeLimitForLevel(level);
}

std::unique_ptr<QuicDecrypter>
TlsClientHandshaker::AdvanceKeysAndCreateCurrentOneRttDecrypter() {
  return TlsHandshaker::AdvanceKeysAndCreateCurrentOneRttDecrypter();
}

std::unique_ptr<QuicEncrypter>
TlsClientHandshaker::CreateCurrentOneRttEncrypter() {
  return TlsHandshaker::CreateCurrentOneRttEncrypter();
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

void TlsClientHandshaker::OnConnectionClosed(QuicErrorCode error,
                                             ConnectionCloseSource source) {
  TlsHandshaker::OnConnectionClosed(error, source);
}

void TlsClientHandshaker::OnHandshakeDoneReceived() {
  if (!one_rtt_keys_available()) {
    CloseConnection(QUIC_HANDSHAKE_FAILED,
                    "Unexpected handshake done received");
    return;
  }
  OnHandshakeConfirmed();
}

void TlsClientHandshaker::OnNewTokenReceived(absl::string_view token) {
  if (token.empty()) {
    return;
  }
  if (session_cache_ != nullptr) {
    session_cache_->OnNewTokenReceived(server_id_, token);
  }
}

void TlsClientHandshaker::SetWriteSecret(
    EncryptionLevel level, const SSL_CIPHER* cipher,
    absl::Span<const uint8_t> write_secret) {
  if (is_connection_closed()) {
    return;
  }
  if (level == ENCRYPTION_FORWARD_SECURE || level == ENCRYPTION_ZERO_RTT) {
    encryption_established_ = true;
  }
  TlsHandshaker::SetWriteSecret(level, cipher, write_secret);
  if (level == ENCRYPTION_FORWARD_SECURE) {
    handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_ZERO_RTT);
  }
}

void TlsClientHandshaker::OnHandshakeConfirmed() {
  QUICHE_DCHECK(one_rtt_keys_available());
  if (state_ >= HANDSHAKE_CONFIRMED) {
    return;
  }
  state_ = HANDSHAKE_CONFIRMED;
  handshaker_delegate()->DiscardOldEncryptionKey(ENCRYPTION_HANDSHAKE);
  handshaker_delegate()->DiscardOldDecryptionKey(ENCRYPTION_HANDSHAKE);
}

QuicAsyncStatus TlsClientHandshaker::VerifyCertChain(
    const std::vector<std::string>& certs, std::string* error_details,
    std::unique_ptr<ProofVerifyDetails>* details, uint8_t* out_alert,
    std::unique_ptr<ProofVerifierCallback> callback) {
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

  return proof_verifier_->VerifyCertChain(
      server_id_.host(), server_id_.port(), certs, ocsp_response, sct_list,
      verify_context_.get(), error_details, details, out_alert,
      std::move(callback));
}

void TlsClientHandshaker::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& verify_details) {
  proof_handler_->OnProofVerifyDetailsAvailable(verify_details);
}

void TlsClientHandshaker::FinishHandshake() {
  FillNegotiatedParams();

  QUICHE_CHECK(!SSL_in_early_data(ssl()));

  QUIC_LOG(INFO) << "Client: handshake finished";

  std::string error_details;
  if (!ProcessTransportParameters(&error_details)) {
    QUICHE_DCHECK(!error_details.empty());
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

  // Parse ALPS extension.
  const uint8_t* alps_data;
  size_t alps_length;
  SSL_get0_peer_application_settings(ssl(), &alps_data, &alps_length);
  if (alps_length > 0) {
    auto error = session()->OnAlpsData(alps_data, alps_length);
    if (error.has_value()) {
      // Calling CloseConnection() is safe even in case OnAlpsData() has
      // already closed the connection.
      CloseConnection(QUIC_HANDSHAKE_FAILED,
                      absl::StrCat("Error processing ALPS data: ", *error));
      return;
    }
  }

  state_ = HANDSHAKE_COMPLETE;
  handshaker_delegate()->OnTlsHandshakeComplete();
}

void TlsClientHandshaker::OnEnterEarlyData() {
  QUICHE_DCHECK(SSL_in_early_data(ssl()));

  // TODO(wub): It might be unnecessary to FillNegotiatedParams() at this time,
  // because we fill it again when handshake completes.
  FillNegotiatedParams();

  // If we're attempting a 0-RTT handshake, then we need to let the transport
  // and application know what state to apply to early data.
  PrepareZeroRttConfig(cached_state_.get());
}

void TlsClientHandshaker::FillNegotiatedParams() {
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl());
  if (cipher) {
    crypto_negotiated_params_->cipher_suite =
        SSL_CIPHER_get_protocol_id(cipher);
  }
  crypto_negotiated_params_->key_exchange_group = SSL_get_curve_id(ssl());
  crypto_negotiated_params_->peer_signature_algorithm =
      SSL_get_peer_signature_algorithm(ssl());
  crypto_negotiated_params_->encrypted_client_hello = SSL_ech_accepted(ssl());
}

void TlsClientHandshaker::ProcessPostHandshakeMessage() {
  int rv = SSL_process_quic_post_handshake(ssl());
  if (rv != 1) {
    CloseConnection(QUIC_HANDSHAKE_FAILED, "Unexpected post-handshake data");
  }
}

bool TlsClientHandshaker::ShouldCloseConnectionOnUnexpectedError(
    int ssl_error) {
  if (ssl_error != SSL_ERROR_EARLY_DATA_REJECTED) {
    return true;
  }
  HandleZeroRttReject();
  return false;
}

void TlsClientHandshaker::HandleZeroRttReject() {
  QUIC_LOG(INFO) << "0-RTT handshake attempted but was rejected by the server";
  QUICHE_DCHECK(session_cache_);
  // Disable encrytion to block outgoing data until 1-RTT keys are available.
  encryption_established_ = false;
  handshaker_delegate()->OnZeroRttRejected(EarlyDataReason());
  SSL_reset_early_data_reject(ssl());
  session_cache_->ClearEarlyData(server_id_);
  AdvanceHandshake();
}

void TlsClientHandshaker::InsertSession(bssl::UniquePtr<SSL_SESSION> session) {
  if (!received_transport_params_) {
    QUIC_BUG(quic_bug_10576_8) << "Transport parameters isn't received";
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
                                       absl::string_view data) {
  if (level == ENCRYPTION_HANDSHAKE && state_ < HANDSHAKE_PROCESSED) {
    state_ = HANDSHAKE_PROCESSED;
  }
  TlsHandshaker::WriteMessage(level, data);
}

void TlsClientHandshaker::SetServerApplicationStateForResumption(
    std::unique_ptr<ApplicationState> application_state) {
  QUICHE_DCHECK(one_rtt_keys_available());
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
