// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_dispatcher.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_version_manager.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_server_backend.h"
#include "quiche/quic/masque/masque_server_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_simple_dispatcher.h"

namespace quic {

MasqueDispatcher::MasqueDispatcher(
    MasqueMode masque_mode, const QuicConfig* config,
    const QuicCryptoServerConfig* crypto_config,
    QuicVersionManager* version_manager, QuicEventLoop* event_loop,
    std::unique_ptr<QuicConnectionHelperInterface> helper,
    std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper,
    std::unique_ptr<QuicAlarmFactory> alarm_factory,
    MasqueServerBackend* masque_server_backend,
    uint8_t expected_server_connection_id_length,
    ConnectionIdGeneratorInterface& generator)
    : QuicSimpleDispatcher(config, crypto_config, version_manager,
                           std::move(helper), std::move(session_helper),
                           std::move(alarm_factory), masque_server_backend,
                           expected_server_connection_id_length, generator),
      masque_mode_(masque_mode),
      event_loop_(event_loop),
      masque_server_backend_(masque_server_backend) {}

std::unique_ptr<QuicSession> MasqueDispatcher::CreateQuicSession(
    QuicConnectionId connection_id, const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, absl::string_view /*alpn*/,
    const ParsedQuicVersion& version, const ParsedClientHello& /*parsed_chlo*/,
    ConnectionIdGeneratorInterface& connection_id_generator) {
  // The MasqueServerSession takes ownership of |connection| below.
  QuicConnection* connection = new QuicConnection(
      connection_id, self_address, peer_address, helper(), alarm_factory(),
      writer(),
      /*owns_writer=*/false, Perspective::IS_SERVER,
      ParsedQuicVersionVector{version}, connection_id_generator);

  auto session = std::make_unique<MasqueServerSession>(
      masque_mode_, config(), GetSupportedVersions(), connection, this,
      event_loop_, session_helper(), crypto_config(), compressed_certs_cache(),
      masque_server_backend_);
  session->Initialize();
  return session;
}

}  // namespace quic
