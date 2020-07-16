// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_dispatcher.h"

#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_factory.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QuartcDispatcher::QuartcDispatcher(
    std::unique_ptr<QuicConfig> config,
    std::unique_ptr<QuicCryptoServerConfig> crypto_config,
    QuicVersionManager* version_manager,
    std::unique_ptr<QuicConnectionHelperInterface> helper,
    std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper,
    std::unique_ptr<QuicAlarmFactory> alarm_factory,
    std::unique_ptr<QuartcPacketWriter> packet_writer,
    Delegate* delegate)
    : QuicDispatcher(
          config.get(),
          crypto_config.get(),
          version_manager,
          std::move(helper),
          std::move(session_helper),
          std::move(alarm_factory),
          QuicUtils::CreateZeroConnectionId(
              version_manager->GetSupportedVersions()[0].transport_version)
              .length()),
      owned_quic_config_(std::move(config)),
      owned_crypto_config_(std::move(crypto_config)),
      delegate_(delegate),
      packet_writer_(packet_writer.get()) {
  // Allow incoming packets to set our expected connection ID length.
  SetShouldUpdateExpectedServerConnectionIdLength(true);
  // Allow incoming packets with connection ID lengths shorter than allowed.
  SetAllowShortInitialServerConnectionIds(true);
  // QuicDispatcher takes ownership of the writer.
  QuicDispatcher::InitializeWithWriter(packet_writer.release());
  // NB: This must happen *after* InitializeWithWriter.  It can call us back
  // with OnTransportCanWrite() immediately, and the dispatcher needs to be
  // fully initialized to handle that.
  packet_writer_->SetPacketTransportDelegate(this);
}

QuartcDispatcher::~QuartcDispatcher() {
  packet_writer_->SetPacketTransportDelegate(nullptr);
}

std::unique_ptr<QuicSession> QuartcDispatcher::CreateQuicSession(
    QuicConnectionId connection_id,
    const QuicSocketAddress& client_address,
    quiche::QuicheStringPiece /*alpn*/,
    const ParsedQuicVersion& version) {
  // Make our expected connection ID non-mutable since we have a connection.
  SetShouldUpdateExpectedServerConnectionIdLength(false);
  std::unique_ptr<QuicConnection> connection = CreateQuicConnection(
      connection_id, client_address, helper(), alarm_factory(), writer(),
      Perspective::IS_SERVER, ParsedQuicVersionVector{version});
  auto session = std::make_unique<QuartcServerSession>(
      std::move(connection), /*visitor=*/this, config(), GetSupportedVersions(),
      helper()->GetClock(), crypto_config(), compressed_certs_cache(),
      session_helper());
  delegate_->OnSessionCreated(session.get());
  return session;
}

void QuartcDispatcher::OnTransportCanWrite() {
  OnCanWrite();
}

void QuartcDispatcher::OnTransportReceived(const char* data, size_t data_len) {
  // QuartcPacketTransport does not surface real peer addresses, so the
  // dispatcher uses a dummy address when processing incoming packets. Note that
  // the dispatcher refuses to process anything with port 0.
  static const QuicSocketAddress* dummy_address =
      new QuicSocketAddress(QuicIpAddress::Any4(), /*port=*/1);

  QuicReceivedPacket packet(data, data_len, helper()->GetClock()->Now());
  ProcessPacket(/*self_address=*/*dummy_address,
                /*peer_address=*/*dummy_address, packet);
}

}  // namespace quic
