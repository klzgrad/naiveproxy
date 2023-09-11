// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/qbone_server_session.h"

#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/qbone/qbone_constants.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"

namespace quic {

bool QboneCryptoServerStreamHelper::CanAcceptClientHello(
    const CryptoHandshakeMessage& chlo, const QuicSocketAddress& client_address,
    const QuicSocketAddress& peer_address,
    const QuicSocketAddress& self_address, std::string* error_details) const {
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
    QuicConnection* connection, Visitor* owner, const QuicConfig& config,
    const QuicCryptoServerConfig* quic_crypto_server_config,
    QuicCompressedCertsCache* compressed_certs_cache, QbonePacketWriter* writer,
    QuicIpAddress self_ip, QuicIpAddress client_ip,
    size_t client_ip_subnet_length, QboneServerControlStream::Handler* handler)
    : QboneSessionBase(connection, owner, config, supported_versions, writer),
      processor_(self_ip, client_ip, client_ip_subnet_length, this, this),
      quic_crypto_server_config_(quic_crypto_server_config),
      compressed_certs_cache_(compressed_certs_cache),
      handler_(handler) {}

QboneServerSession::~QboneServerSession() {}

std::unique_ptr<QuicCryptoStream> QboneServerSession::CreateCryptoStream() {
  return CreateCryptoServerStream(quic_crypto_server_config_,
                                  compressed_certs_cache_, this,
                                  &stream_helper_);
}

void QboneServerSession::CreateControlStream() {
  if (control_stream_ != nullptr) {
    return;
  }
  // Register the reserved control stream.
  auto control_stream =
      std::make_unique<QboneServerControlStream>(this, handler_);
  control_stream_ = control_stream.get();
  ActivateStream(std::move(control_stream));
}

QuicStream* QboneServerSession::CreateControlStreamFromPendingStream(
    PendingStream* pending) {
  QUICHE_DCHECK(control_stream_ == nullptr);
  // Register the reserved control stream.
  auto control_stream =
      std::make_unique<QboneServerControlStream>(pending, this, handler_);
  control_stream_ = control_stream.get();
  ActivateStream(std::move(control_stream));
  return control_stream_;
}

void QboneServerSession::SetDefaultEncryptionLevel(
    quic::EncryptionLevel level) {
  QboneSessionBase::SetDefaultEncryptionLevel(level);
  if (level == quic::ENCRYPTION_FORWARD_SECURE) {
    CreateControlStream();
  }
}

bool QboneServerSession::SendClientRequest(const QboneClientRequest& request) {
  if (!control_stream_) {
    QUIC_BUG(quic_bug_11026_1)
        << "Cannot send client request before control stream is created.";
    return false;
  }
  return control_stream_->SendRequest(request);
}

void QboneServerSession::ProcessPacketFromNetwork(absl::string_view packet) {
  std::string buffer = std::string(packet);
  processor_.ProcessPacket(&buffer,
                           QbonePacketProcessor::Direction::FROM_NETWORK);
}

void QboneServerSession::ProcessPacketFromPeer(absl::string_view packet) {
  std::string buffer = std::string(packet);
  processor_.ProcessPacket(&buffer,
                           QbonePacketProcessor::Direction::FROM_OFF_NETWORK);
}

void QboneServerSession::SendPacketToClient(absl::string_view packet) {
  SendPacketToPeer(packet);
}

void QboneServerSession::SendPacketToNetwork(absl::string_view packet) {
  QUICHE_DCHECK(writer_ != nullptr);
  writer_->WritePacketToNetwork(packet.data(), packet.size());
}

}  // namespace quic
