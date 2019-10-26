// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_server_session.h"

#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"

namespace quic {

bool QboneCryptoServerStreamHelper::CanAcceptClientHello(
    const CryptoHandshakeMessage& chlo,
    const QuicSocketAddress& client_address,
    const QuicSocketAddress& peer_address,
    const QuicSocketAddress& self_address,
    string* error_details) const {
  absl::string_view alpn;
  chlo.GetStringPiece(quic::kALPN, &alpn);
  if (alpn != QboneConstants::kQboneAlpn) {
    *error_details = "ALPN-indicated protocol is not qbone";
    return false;
  }
  return true;
}

QboneServerSession::QboneServerSession(
    const quic::ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection,
    Visitor* owner,
    const QuicConfig& config,
    const QuicCryptoServerConfig* quic_crypto_server_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    QbonePacketWriter* writer,
    QuicIpAddress self_ip,
    QuicIpAddress client_ip,
    size_t client_ip_subnet_length,
    QboneServerControlStream::Handler* handler)
    : QboneSessionBase(connection, owner, config, supported_versions, writer),
      processor_(self_ip, client_ip, client_ip_subnet_length, this, this),
      quic_crypto_server_config_(quic_crypto_server_config),
      compressed_certs_cache_(compressed_certs_cache),
      handler_(handler) {}

QboneServerSession::~QboneServerSession() {}

std::unique_ptr<QuicCryptoStream> QboneServerSession::CreateCryptoStream() {
  return QuicMakeUnique<QuicCryptoServerStream>(quic_crypto_server_config_,
                                                compressed_certs_cache_, this,
                                                &stream_helper_);
}

void QboneServerSession::Initialize() {
  QboneSessionBase::Initialize();
  // Register the reserved control stream.
  auto control_stream =
      QuicMakeUnique<QboneServerControlStream>(this, handler_);
  control_stream_ = control_stream.get();
  RegisterStaticStream(std::move(control_stream));
}

bool QboneServerSession::SendClientRequest(const QboneClientRequest& request) {
  if (!control_stream_) {
    QUIC_BUG << "Cannot send client request before control stream is created.";
    return false;
  }
  return control_stream_->SendRequest(request);
}

void QboneServerSession::ProcessPacketFromNetwork(QuicStringPiece packet) {
  string buffer = string(packet);
  processor_.ProcessPacket(&buffer,
                           QbonePacketProcessor::Direction::FROM_NETWORK);
}

void QboneServerSession::ProcessPacketFromPeer(QuicStringPiece packet) {
  string buffer = string(packet);
  processor_.ProcessPacket(&buffer,
                           QbonePacketProcessor::Direction::FROM_CLIENT);
}

void QboneServerSession::SendPacketToClient(QuicStringPiece packet) {
  SendPacketToPeer(packet);
}

void QboneServerSession::SendPacketToNetwork(QuicStringPiece packet) {
  DCHECK(writer_ != nullptr);
  writer_->WritePacketToNetwork(packet.data(), packet.size());
}

}  // namespace quic
