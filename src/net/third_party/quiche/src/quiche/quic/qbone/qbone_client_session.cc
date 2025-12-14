// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/qbone_client_session.h"

#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/qbone/qbone_constants.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"

namespace quic {

QboneClientSession::QboneClientSession(
    QuicConnection* connection,
    QuicCryptoClientConfig* quic_crypto_client_config,
    QuicSession::Visitor* owner, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicServerId& server_id, QbonePacketWriter* writer,
    QboneClientControlStream::Handler* handler)
    : QboneSessionBase(connection, owner, config, supported_versions, writer),
      server_id_(server_id),
      quic_crypto_client_config_(quic_crypto_client_config),
      handler_(handler) {}

QboneClientSession::~QboneClientSession() {}

std::unique_ptr<QuicCryptoStream> QboneClientSession::CreateCryptoStream() {
  return std::make_unique<QuicCryptoClientStream>(
      server_id_, this, nullptr, quic_crypto_client_config_, this,
      /*has_application_state = */ true);
}

void QboneClientSession::CreateControlStream() {
  if (control_stream_ != nullptr) {
    return;
  }
  // Register the reserved control stream.
  QuicStreamId next_id = GetNextOutgoingBidirectionalStreamId();
  QUICHE_DCHECK_EQ(next_id,
                   QboneConstants::GetControlStreamId(transport_version()));
  auto control_stream =
      std::make_unique<QboneClientControlStream>(this, handler_);
  control_stream_ = control_stream.get();
  ActivateStream(std::move(control_stream));
}

void QboneClientSession::Initialize() {
  // Initialize must be called first, as that's what generates the crypto
  // stream.
  QboneSessionBase::Initialize();
  static_cast<QuicCryptoClientStreamBase*>(GetMutableCryptoStream())
      ->CryptoConnect();
}

void QboneClientSession::SetDefaultEncryptionLevel(
    quic::EncryptionLevel level) {
  QboneSessionBase::SetDefaultEncryptionLevel(level);
  if (level == quic::ENCRYPTION_FORWARD_SECURE) {
    CreateControlStream();
  }
}

int QboneClientSession::GetNumSentClientHellos() const {
  return static_cast<const QuicCryptoClientStreamBase*>(GetCryptoStream())
      ->num_sent_client_hellos();
}

bool QboneClientSession::EarlyDataAccepted() const {
  return static_cast<const QuicCryptoClientStreamBase*>(GetCryptoStream())
      ->EarlyDataAccepted();
}

bool QboneClientSession::ReceivedInchoateReject() const {
  return static_cast<const QuicCryptoClientStreamBase*>(GetCryptoStream())
      ->ReceivedInchoateReject();
}

int QboneClientSession::GetNumReceivedServerConfigUpdates() const {
  return static_cast<const QuicCryptoClientStreamBase*>(GetCryptoStream())
      ->num_scup_messages_received();
}

bool QboneClientSession::SendServerRequest(const QboneServerRequest& request) {
  if (!control_stream_) {
    QUIC_BUG(quic_bug_11056_1)
        << "Cannot send server request before control stream is created.";
    return false;
  }
  return control_stream_->SendRequest(request);
}

void QboneClientSession::ProcessPacketFromNetwork(absl::string_view packet) {
  SendPacketToPeer(packet);
}

void QboneClientSession::ProcessPacketFromPeer(absl::string_view packet) {
  writer_->WritePacketToNetwork(packet.data(), packet.size());
}

void QboneClientSession::OnProofValid(
    const QuicCryptoClientConfig::CachedState& cached) {}

void QboneClientSession::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& verify_details) {}

bool QboneClientSession::HasActiveRequests() const {
  return GetNumActiveStreams() + num_draining_streams() > 0;
}

}  // namespace quic
