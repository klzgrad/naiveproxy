// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/quic_crypto_client_stream.h"

#include <memory>
#include <string>
#include <utility>

#include "quic/core/crypto/crypto_protocol.h"
#include "quic/core/crypto/crypto_utils.h"
#include "quic/core/crypto/null_encrypter.h"
#include "quic/core/crypto/quic_crypto_client_config.h"
#include "quic/core/quic_crypto_client_handshaker.h"
#include "quic/core/quic_packets.h"
#include "quic/core/quic_session.h"
#include "quic/core/quic_utils.h"
#include "quic/core/tls_client_handshaker.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {

const int QuicCryptoClientStream::kMaxClientHellos;

QuicCryptoClientStreamBase::QuicCryptoClientStreamBase(QuicSession* session)
    : QuicCryptoStream(session) {}

QuicCryptoClientStream::QuicCryptoClientStream(
    const QuicServerId& server_id,
    QuicSession* session,
    std::unique_ptr<ProofVerifyContext> verify_context,
    QuicCryptoClientConfig* crypto_config,
    ProofHandler* proof_handler,
    bool has_application_state)
    : QuicCryptoClientStreamBase(session) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT,
                   session->connection()->perspective());
  switch (session->connection()->version().handshake_protocol) {
    case PROTOCOL_QUIC_CRYPTO:
      handshaker_ = std::make_unique<QuicCryptoClientHandshaker>(
          server_id, this, session, std::move(verify_context), crypto_config,
          proof_handler);
      break;
    case PROTOCOL_TLS1_3:
      handshaker_ = std::make_unique<TlsClientHandshaker>(
          server_id, this, session, std::move(verify_context), crypto_config,
          proof_handler, has_application_state);
      break;
    case PROTOCOL_UNSUPPORTED:
      QUIC_BUG << "Attempting to create QuicCryptoClientStream for unknown "
                  "handshake protocol";
  }
}

QuicCryptoClientStream::~QuicCryptoClientStream() {}

bool QuicCryptoClientStream::CryptoConnect() {
  return handshaker_->CryptoConnect();
}

int QuicCryptoClientStream::num_sent_client_hellos() const {
  return handshaker_->num_sent_client_hellos();
}

bool QuicCryptoClientStream::IsResumption() const {
  return handshaker_->IsResumption();
}

bool QuicCryptoClientStream::EarlyDataAccepted() const {
  return handshaker_->EarlyDataAccepted();
}

ssl_early_data_reason_t QuicCryptoClientStream::EarlyDataReason() const {
  return handshaker_->EarlyDataReason();
}

bool QuicCryptoClientStream::ReceivedInchoateReject() const {
  return handshaker_->ReceivedInchoateReject();
}

int QuicCryptoClientStream::num_scup_messages_received() const {
  return handshaker_->num_scup_messages_received();
}

bool QuicCryptoClientStream::encryption_established() const {
  return handshaker_->encryption_established();
}

bool QuicCryptoClientStream::one_rtt_keys_available() const {
  return handshaker_->one_rtt_keys_available();
}

const QuicCryptoNegotiatedParameters&
QuicCryptoClientStream::crypto_negotiated_params() const {
  return handshaker_->crypto_negotiated_params();
}

CryptoMessageParser* QuicCryptoClientStream::crypto_message_parser() {
  return handshaker_->crypto_message_parser();
}

HandshakeState QuicCryptoClientStream::GetHandshakeState() const {
  return handshaker_->GetHandshakeState();
}

size_t QuicCryptoClientStream::BufferSizeLimitForLevel(
    EncryptionLevel level) const {
  return handshaker_->BufferSizeLimitForLevel(level);
}

bool QuicCryptoClientStream::KeyUpdateSupportedLocally() const {
  return handshaker_->KeyUpdateSupportedLocally();
}

std::unique_ptr<QuicDecrypter>
QuicCryptoClientStream::AdvanceKeysAndCreateCurrentOneRttDecrypter() {
  return handshaker_->AdvanceKeysAndCreateCurrentOneRttDecrypter();
}

std::unique_ptr<QuicEncrypter>
QuicCryptoClientStream::CreateCurrentOneRttEncrypter() {
  return handshaker_->CreateCurrentOneRttEncrypter();
}

std::string QuicCryptoClientStream::chlo_hash() const {
  return handshaker_->chlo_hash();
}

void QuicCryptoClientStream::OnOneRttPacketAcknowledged() {
  handshaker_->OnOneRttPacketAcknowledged();
}

void QuicCryptoClientStream::OnHandshakePacketSent() {
  handshaker_->OnHandshakePacketSent();
}

void QuicCryptoClientStream::OnConnectionClosed(QuicErrorCode error,
                                                ConnectionCloseSource source) {
  handshaker_->OnConnectionClosed(error, source);
}

void QuicCryptoClientStream::OnHandshakeDoneReceived() {
  handshaker_->OnHandshakeDoneReceived();
}

void QuicCryptoClientStream::OnNewTokenReceived(absl::string_view token) {
  handshaker_->OnNewTokenReceived(token);
}

std::string QuicCryptoClientStream::GetAddressToken() const {
  QUICHE_DCHECK(false);
  return "";
}

bool QuicCryptoClientStream::ValidateAddressToken(
    absl::string_view /*token*/) const {
  QUICHE_DCHECK(false);
  return false;
}

void QuicCryptoClientStream::SetServerApplicationStateForResumption(
    std::unique_ptr<ApplicationState> application_state) {
  handshaker_->SetServerApplicationStateForResumption(
      std::move(application_state));
}

}  // namespace quic
