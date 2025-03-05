// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_crypto_server_stream.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "openssl/sha.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_testvalue.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

class QuicCryptoServerStream::ProcessClientHelloCallback
    : public ProcessClientHelloResultCallback {
 public:
  ProcessClientHelloCallback(
      QuicCryptoServerStream* parent,
      const quiche::QuicheReferenceCountedPointer<
          ValidateClientHelloResultCallback::Result>& result)
      : parent_(parent), result_(result) {}

  void Run(
      QuicErrorCode error, const std::string& error_details,
      std::unique_ptr<CryptoHandshakeMessage> message,
      std::unique_ptr<DiversificationNonce> diversification_nonce,
      std::unique_ptr<ProofSource::Details> proof_source_details) override {
    if (parent_ == nullptr) {
      return;
    }

    parent_->FinishProcessingHandshakeMessageAfterProcessClientHello(
        *result_, error, error_details, std::move(message),
        std::move(diversification_nonce), std::move(proof_source_details));
  }

  void Cancel() { parent_ = nullptr; }

 private:
  QuicCryptoServerStream* parent_;
  quiche::QuicheReferenceCountedPointer<
      ValidateClientHelloResultCallback::Result>
      result_;
};

QuicCryptoServerStream::QuicCryptoServerStream(
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache, QuicSession* session,
    QuicCryptoServerStreamBase::Helper* helper)
    : QuicCryptoServerStreamBase(session),
      QuicCryptoHandshaker(this, session),
      session_(session),
      delegate_(session),
      crypto_config_(crypto_config),
      compressed_certs_cache_(compressed_certs_cache),
      signed_config_(new QuicSignedServerConfig),
      helper_(helper),
      num_handshake_messages_(0),
      num_handshake_messages_with_server_nonces_(0),
      send_server_config_update_cb_(nullptr),
      num_server_config_update_messages_sent_(0),
      zero_rtt_attempted_(false),
      chlo_packet_size_(0),
      validate_client_hello_cb_(nullptr),
      encryption_established_(false),
      one_rtt_keys_available_(false),
      one_rtt_packet_decrypted_(false),
      crypto_negotiated_params_(new QuicCryptoNegotiatedParameters) {}

QuicCryptoServerStream::~QuicCryptoServerStream() {
  CancelOutstandingCallbacks();
}

void QuicCryptoServerStream::CancelOutstandingCallbacks() {
  // Detach from the validation callback.  Calling this multiple times is safe.
  if (validate_client_hello_cb_ != nullptr) {
    validate_client_hello_cb_->Cancel();
    validate_client_hello_cb_ = nullptr;
  }
  if (send_server_config_update_cb_ != nullptr) {
    send_server_config_update_cb_->Cancel();
    send_server_config_update_cb_ = nullptr;
  }
  if (std::shared_ptr<ProcessClientHelloCallback> cb =
          process_client_hello_cb_.lock()) {
    cb->Cancel();
    process_client_hello_cb_.reset();
  }
}

void QuicCryptoServerStream::OnHandshakeMessage(
    const CryptoHandshakeMessage& message) {
  QuicCryptoHandshaker::OnHandshakeMessage(message);
  ++num_handshake_messages_;
  chlo_packet_size_ = session()->connection()->GetCurrentPacket().length();

  // Do not process handshake messages after the handshake is confirmed.
  if (one_rtt_keys_available_) {
    OnUnrecoverableError(QUIC_CRYPTO_MESSAGE_AFTER_HANDSHAKE_COMPLETE,
                         "Unexpected handshake message from client");
    return;
  }

  if (message.tag() != kCHLO) {
    OnUnrecoverableError(QUIC_INVALID_CRYPTO_MESSAGE_TYPE,
                         "Handshake packet not CHLO");
    return;
  }

  if (validate_client_hello_cb_ != nullptr ||
      !process_client_hello_cb_.expired()) {
    // Already processing some other handshake message.  The protocol
    // does not allow for clients to send multiple handshake messages
    // before the server has a chance to respond.
    OnUnrecoverableError(QUIC_CRYPTO_MESSAGE_WHILE_VALIDATING_CLIENT_HELLO,
                         "Unexpected handshake message while processing CHLO");
    return;
  }

  chlo_hash_ =
      CryptoUtils::HashHandshakeMessage(message, Perspective::IS_SERVER);

  std::unique_ptr<ValidateCallback> cb(new ValidateCallback(this));
  QUICHE_DCHECK(validate_client_hello_cb_ == nullptr);
  QUICHE_DCHECK(process_client_hello_cb_.expired());
  validate_client_hello_cb_ = cb.get();
  crypto_config_->ValidateClientHello(
      message, GetClientAddress(), session()->connection()->self_address(),
      transport_version(), session()->connection()->clock(), signed_config_,
      std::move(cb));
}

void QuicCryptoServerStream::FinishProcessingHandshakeMessage(
    quiche::QuicheReferenceCountedPointer<
        ValidateClientHelloResultCallback::Result>
        result,
    std::unique_ptr<ProofSource::Details> details) {
  // Clear the callback that got us here.
  QUICHE_DCHECK(validate_client_hello_cb_ != nullptr);
  QUICHE_DCHECK(process_client_hello_cb_.expired());
  validate_client_hello_cb_ = nullptr;

  auto cb = std::make_shared<ProcessClientHelloCallback>(this, result);
  process_client_hello_cb_ = cb;
  ProcessClientHello(result, std::move(details), std::move(cb));
}

void QuicCryptoServerStream::
    FinishProcessingHandshakeMessageAfterProcessClientHello(
        const ValidateClientHelloResultCallback::Result& result,
        QuicErrorCode error, const std::string& error_details,
        std::unique_ptr<CryptoHandshakeMessage> reply,
        std::unique_ptr<DiversificationNonce> diversification_nonce,
        std::unique_ptr<ProofSource::Details> proof_source_details) {
  // Clear the callback that got us here.
  QUICHE_DCHECK(!process_client_hello_cb_.expired());
  QUICHE_DCHECK(validate_client_hello_cb_ == nullptr);
  process_client_hello_cb_.reset();
  proof_source_details_ = std::move(proof_source_details);

  AdjustTestValue("quic::QuicCryptoServerStream::after_process_client_hello",
                  session());

  if (!session()->connection()->connected()) {
    QUIC_CODE_COUNT(quic_crypto_disconnected_after_process_client_hello);
    QUIC_LOG_FIRST_N(INFO, 10)
        << "After processing CHLO, QUIC connection has been closed with code "
        << session()->error() << ", details: " << session()->error_details();
    return;
  }

  const CryptoHandshakeMessage& message = result.client_hello;
  if (error != QUIC_NO_ERROR) {
    OnUnrecoverableError(error, error_details);
    return;
  }

  if (reply->tag() != kSHLO) {
    session()->connection()->set_fully_pad_crypto_handshake_packets(
        crypto_config_->pad_rej());
    // Send REJ in plaintext.
    SendHandshakeMessage(*reply, ENCRYPTION_INITIAL);
    return;
  }

  // If we are returning a SHLO then we accepted the handshake.  Now
  // process the negotiated configuration options as part of the
  // session config.
  QuicConfig* config = session()->config();
  OverrideQuicConfigDefaults(config);
  std::string process_error_details;
  const QuicErrorCode process_error =
      config->ProcessPeerHello(message, CLIENT, &process_error_details);
  if (process_error != QUIC_NO_ERROR) {
    OnUnrecoverableError(process_error, process_error_details);
    return;
  }

  session()->OnConfigNegotiated();

  config->ToHandshakeMessage(reply.get(), session()->transport_version());

  // Receiving a full CHLO implies the client is prepared to decrypt with
  // the new server write key.  We can start to encrypt with the new server
  // write key.
  //
  // NOTE: the SHLO will be encrypted with the new server write key.
  delegate_->OnNewEncryptionKeyAvailable(
      ENCRYPTION_ZERO_RTT,
      std::move(crypto_negotiated_params_->initial_crypters.encrypter));
  delegate_->OnNewDecryptionKeyAvailable(
      ENCRYPTION_ZERO_RTT,
      std::move(crypto_negotiated_params_->initial_crypters.decrypter),
      /*set_alternative_decrypter=*/false,
      /*latch_once_used=*/false);
  delegate_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  delegate_->DiscardOldDecryptionKey(ENCRYPTION_INITIAL);
  session()->connection()->SetDiversificationNonce(*diversification_nonce);

  session()->connection()->set_fully_pad_crypto_handshake_packets(
      crypto_config_->pad_shlo());
  // Send SHLO in ENCRYPTION_ZERO_RTT.
  SendHandshakeMessage(*reply, ENCRYPTION_ZERO_RTT);
  delegate_->OnNewEncryptionKeyAvailable(
      ENCRYPTION_FORWARD_SECURE,
      std::move(crypto_negotiated_params_->forward_secure_crypters.encrypter));
  delegate_->OnNewDecryptionKeyAvailable(
      ENCRYPTION_FORWARD_SECURE,
      std::move(crypto_negotiated_params_->forward_secure_crypters.decrypter),
      /*set_alternative_decrypter=*/true,
      /*latch_once_used=*/false);
  encryption_established_ = true;
  one_rtt_keys_available_ = true;
  delegate_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  delegate_->DiscardOldEncryptionKey(ENCRYPTION_INITIAL);
}

void QuicCryptoServerStream::SendServerConfigUpdate(
    const CachedNetworkParameters* cached_network_params) {
  if (!one_rtt_keys_available_) {
    return;
  }

  if (send_server_config_update_cb_ != nullptr) {
    QUIC_DVLOG(1)
        << "Skipped server config update since one is already in progress";
    return;
  }

  std::unique_ptr<SendServerConfigUpdateCallback> cb(
      new SendServerConfigUpdateCallback(this));
  send_server_config_update_cb_ = cb.get();

  crypto_config_->BuildServerConfigUpdateMessage(
      session()->transport_version(), chlo_hash_,
      previous_source_address_tokens_, session()->connection()->self_address(),
      GetClientAddress(), session()->connection()->clock(),
      session()->connection()->random_generator(), compressed_certs_cache_,
      *crypto_negotiated_params_, cached_network_params, std::move(cb));
}

QuicCryptoServerStream::SendServerConfigUpdateCallback::
    SendServerConfigUpdateCallback(QuicCryptoServerStream* parent)
    : parent_(parent) {}

void QuicCryptoServerStream::SendServerConfigUpdateCallback::Cancel() {
  parent_ = nullptr;
}

// From BuildServerConfigUpdateMessageResultCallback
void QuicCryptoServerStream::SendServerConfigUpdateCallback::Run(
    bool ok, const CryptoHandshakeMessage& message) {
  if (parent_ == nullptr) {
    return;
  }
  parent_->FinishSendServerConfigUpdate(ok, message);
}

void QuicCryptoServerStream::FinishSendServerConfigUpdate(
    bool ok, const CryptoHandshakeMessage& message) {
  // Clear the callback that got us here.
  QUICHE_DCHECK(send_server_config_update_cb_ != nullptr);
  send_server_config_update_cb_ = nullptr;

  if (!ok) {
    QUIC_DVLOG(1) << "Server: Failed to build server config update (SCUP)!";
    return;
  }

  QUIC_DVLOG(1) << "Server: Sending server config update: "
                << message.DebugString();

  // Send server config update in ENCRYPTION_FORWARD_SECURE.
  SendHandshakeMessage(message, ENCRYPTION_FORWARD_SECURE);

  ++num_server_config_update_messages_sent_;
}

bool QuicCryptoServerStream::DisableResumption() {
  QUICHE_DCHECK(false) << "Not supported for QUIC crypto.";
  return false;
}

bool QuicCryptoServerStream::IsZeroRtt() const {
  return num_handshake_messages_ == 1 &&
         num_handshake_messages_with_server_nonces_ == 0;
}

bool QuicCryptoServerStream::IsResumption() const {
  // QUIC Crypto doesn't have a non-0-RTT resumption mode.
  return IsZeroRtt();
}

int QuicCryptoServerStream::NumServerConfigUpdateMessagesSent() const {
  return num_server_config_update_messages_sent_;
}

const CachedNetworkParameters*
QuicCryptoServerStream::PreviousCachedNetworkParams() const {
  return previous_cached_network_params_.get();
}

bool QuicCryptoServerStream::ResumptionAttempted() const {
  return zero_rtt_attempted_;
}

bool QuicCryptoServerStream::EarlyDataAttempted() const {
  QUICHE_DCHECK(false) << "Not supported for QUIC crypto.";
  return zero_rtt_attempted_;
}

void QuicCryptoServerStream::SetPreviousCachedNetworkParams(
    CachedNetworkParameters cached_network_params) {
  previous_cached_network_params_.reset(
      new CachedNetworkParameters(cached_network_params));
}

void QuicCryptoServerStream::OnPacketDecrypted(EncryptionLevel level) {
  if (level == ENCRYPTION_FORWARD_SECURE) {
    one_rtt_packet_decrypted_ = true;
    delegate_->NeuterHandshakeData();
  }
}

void QuicCryptoServerStream::OnHandshakeDoneReceived() { QUICHE_DCHECK(false); }

void QuicCryptoServerStream::OnNewTokenReceived(absl::string_view /*token*/) {
  QUICHE_DCHECK(false);
}

std::string QuicCryptoServerStream::GetAddressToken(
    const CachedNetworkParameters* /*cached_network_parameters*/) const {
  QUICHE_DCHECK(false);
  return "";
}

bool QuicCryptoServerStream::ValidateAddressToken(
    absl::string_view /*token*/) const {
  QUICHE_DCHECK(false);
  return false;
}

bool QuicCryptoServerStream::ShouldSendExpectCTHeader() const {
  return signed_config_->proof.send_expect_ct_header;
}

bool QuicCryptoServerStream::DidCertMatchSni() const {
  return signed_config_->proof.cert_matched_sni;
}

const ProofSource::Details* QuicCryptoServerStream::ProofSourceDetails() const {
  return proof_source_details_.get();
}

bool QuicCryptoServerStream::GetBase64SHA256ClientChannelID(
    std::string* output) const {
  if (!encryption_established() ||
      crypto_negotiated_params_->channel_id.empty()) {
    return false;
  }

  const std::string& channel_id(crypto_negotiated_params_->channel_id);
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const uint8_t*>(channel_id.data()), channel_id.size(),
         digest);

  quiche::QuicheTextUtils::Base64Encode(digest, ABSL_ARRAYSIZE(digest), output);
  return true;
}

ssl_early_data_reason_t QuicCryptoServerStream::EarlyDataReason() const {
  if (IsZeroRtt()) {
    return ssl_early_data_accepted;
  }
  if (zero_rtt_attempted_) {
    return ssl_early_data_session_not_resumed;
  }
  return ssl_early_data_no_session_offered;
}

bool QuicCryptoServerStream::encryption_established() const {
  return encryption_established_;
}

bool QuicCryptoServerStream::one_rtt_keys_available() const {
  return one_rtt_keys_available_;
}

const QuicCryptoNegotiatedParameters&
QuicCryptoServerStream::crypto_negotiated_params() const {
  return *crypto_negotiated_params_;
}

CryptoMessageParser* QuicCryptoServerStream::crypto_message_parser() {
  return QuicCryptoHandshaker::crypto_message_parser();
}

HandshakeState QuicCryptoServerStream::GetHandshakeState() const {
  return one_rtt_packet_decrypted_ ? HANDSHAKE_COMPLETE : HANDSHAKE_START;
}

void QuicCryptoServerStream::SetServerApplicationStateForResumption(
    std::unique_ptr<ApplicationState> /*state*/) {
  // QUIC Crypto doesn't need to remember any application state as part of doing
  // 0-RTT resumption, so this function is a no-op.
}

size_t QuicCryptoServerStream::BufferSizeLimitForLevel(
    EncryptionLevel level) const {
  return QuicCryptoHandshaker::BufferSizeLimitForLevel(level);
}

std::unique_ptr<QuicDecrypter>
QuicCryptoServerStream::AdvanceKeysAndCreateCurrentOneRttDecrypter() {
  // Key update is only defined in QUIC+TLS.
  QUICHE_DCHECK(false);
  return nullptr;
}

std::unique_ptr<QuicEncrypter>
QuicCryptoServerStream::CreateCurrentOneRttEncrypter() {
  // Key update is only defined in QUIC+TLS.
  QUICHE_DCHECK(false);
  return nullptr;
}

void QuicCryptoServerStream::ProcessClientHello(
    quiche::QuicheReferenceCountedPointer<
        ValidateClientHelloResultCallback::Result>
        result,
    std::unique_ptr<ProofSource::Details> proof_source_details,
    std::shared_ptr<ProcessClientHelloResultCallback> done_cb) {
  proof_source_details_ = std::move(proof_source_details);
  const CryptoHandshakeMessage& message = result->client_hello;
  std::string error_details;
  if (!helper_->CanAcceptClientHello(
          message, GetClientAddress(), session()->connection()->peer_address(),
          session()->connection()->self_address(), &error_details)) {
    done_cb->Run(QUIC_HANDSHAKE_FAILED, error_details, nullptr, nullptr,
                 nullptr);
    return;
  }

  absl::string_view user_agent_id;
  message.GetStringPiece(quic::kUAID, &user_agent_id);
  if (!session()->user_agent_id().has_value() && !user_agent_id.empty()) {
    session()->SetUserAgentId(std::string(user_agent_id));
  }

  if (!result->info.server_nonce.empty()) {
    ++num_handshake_messages_with_server_nonces_;
  }

  if (num_handshake_messages_ == 1) {
    // Client attempts zero RTT handshake by sending a non-inchoate CHLO.
    absl::string_view public_value;
    zero_rtt_attempted_ = message.GetStringPiece(kPUBS, &public_value);
  }

  // Store the bandwidth estimate from the client.
  if (result->cached_network_params.bandwidth_estimate_bytes_per_second() > 0) {
    previous_cached_network_params_.reset(
        new CachedNetworkParameters(result->cached_network_params));
  }
  previous_source_address_tokens_ = result->info.source_address_tokens;

  QuicConnection* connection = session()->connection();
  crypto_config_->ProcessClientHello(
      result, /*reject_only=*/false, connection->connection_id(),
      connection->self_address(), GetClientAddress(), connection->version(),
      session()->supported_versions(), connection->clock(),
      connection->random_generator(), compressed_certs_cache_,
      crypto_negotiated_params_, signed_config_,
      QuicCryptoStream::CryptoMessageFramingOverhead(
          transport_version(), connection->connection_id()),
      chlo_packet_size_, std::move(done_cb));
}

void QuicCryptoServerStream::OverrideQuicConfigDefaults(
    QuicConfig* /*config*/) {}

QuicCryptoServerStream::ValidateCallback::ValidateCallback(
    QuicCryptoServerStream* parent)
    : parent_(parent) {}

void QuicCryptoServerStream::ValidateCallback::Cancel() { parent_ = nullptr; }

void QuicCryptoServerStream::ValidateCallback::Run(
    quiche::QuicheReferenceCountedPointer<Result> result,
    std::unique_ptr<ProofSource::Details> details) {
  if (parent_ != nullptr) {
    parent_->FinishProcessingHandshakeMessage(std::move(result),
                                              std::move(details));
  }
}

const QuicSocketAddress QuicCryptoServerStream::GetClientAddress() {
  return session()->connection()->peer_address();
}

SSL* QuicCryptoServerStream::GetSsl() const { return nullptr; }

bool QuicCryptoServerStream::IsCryptoFrameExpectedForEncryptionLevel(
    EncryptionLevel /*level*/) const {
  return true;
}

EncryptionLevel
QuicCryptoServerStream::GetEncryptionLevelToSendCryptoDataOfSpace(
    PacketNumberSpace space) const {
  if (space == INITIAL_DATA) {
    return ENCRYPTION_INITIAL;
  }
  if (space == APPLICATION_DATA) {
    return ENCRYPTION_ZERO_RTT;
  }
  QUICHE_DCHECK(false);
  return NUM_ENCRYPTION_LEVELS;
}

}  // namespace quic
