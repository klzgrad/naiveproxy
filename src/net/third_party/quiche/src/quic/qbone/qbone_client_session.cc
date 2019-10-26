// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_client_session.h"

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"

namespace quic {

QboneClientSession::QboneClientSession(
    QuicConnection* connection,
    QuicCryptoClientConfig* quic_crypto_client_config,
    QuicSession::Visitor* owner,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicServerId& server_id,
    QbonePacketWriter* writer,
    QboneClientControlStream::Handler* handler)
    : QboneSessionBase(connection, owner, config, supported_versions, writer),
      server_id_(server_id),
      quic_crypto_client_config_(quic_crypto_client_config),
      handler_(handler) {}

QboneClientSession::~QboneClientSession() {}

std::unique_ptr<QuicCryptoStream> QboneClientSession::CreateCryptoStream() {
  return QuicMakeUnique<QuicCryptoClientStream>(
      server_id_, this, nullptr, quic_crypto_client_config_, this);
}

void QboneClientSession::Initialize() {
  // Initialize must be called first, as that's what generates the crypto
  // stream.
  QboneSessionBase::Initialize();
  static_cast<QuicCryptoClientStreamBase*>(GetMutableCryptoStream())
      ->CryptoConnect();
  // Register the reserved control stream.
  QuicStreamId next_id = GetNextOutgoingBidirectionalStreamId();
  DCHECK_EQ(next_id, QboneConstants::GetControlStreamId(
                         connection()->transport_version()));
  auto control_stream =
      QuicMakeUnique<QboneClientControlStream>(this, handler_);
  control_stream_ = control_stream.get();
  RegisterStaticStream(std::move(control_stream));
}

int QboneClientSession::GetNumSentClientHellos() const {
  return static_cast<const QuicCryptoClientStreamBase*>(GetCryptoStream())
      ->num_sent_client_hellos();
}

int QboneClientSession::GetNumReceivedServerConfigUpdates() const {
  return static_cast<const QuicCryptoClientStreamBase*>(GetCryptoStream())
      ->num_scup_messages_received();
}

bool QboneClientSession::SendServerRequest(const QboneServerRequest& request) {
  if (!control_stream_) {
    QUIC_BUG << "Cannot send server request before control stream is created.";
    return false;
  }
  return control_stream_->SendRequest(request);
}

void QboneClientSession::ProcessPacketFromNetwork(QuicStringPiece packet) {
  SendPacketToPeer(packet);
}

void QboneClientSession::ProcessPacketFromPeer(QuicStringPiece packet) {
  writer_->WritePacketToNetwork(packet.data(), packet.size());
}

void QboneClientSession::OnProofValid(
    const QuicCryptoClientConfig::CachedState& cached) {}

void QboneClientSession::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& verify_details) {}

bool QboneClientSession::HasActiveRequests() const {
  return (stream_map().size() - num_incoming_static_streams() -
          num_outgoing_static_streams()) > 0;
}

}  // namespace quic
