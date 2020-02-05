// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_transport_simple_server_dispatcher.h"

#include <memory>

#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_dispatcher.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/tools/quic_transport_simple_server_session.h"

namespace quic {

QuicTransportSimpleServerDispatcher::QuicTransportSimpleServerDispatcher(
    const QuicConfig* config,
    const QuicCryptoServerConfig* crypto_config,
    QuicVersionManager* version_manager,
    std::unique_ptr<QuicConnectionHelperInterface> helper,
    std::unique_ptr<QuicCryptoServerStream::Helper> session_helper,
    std::unique_ptr<QuicAlarmFactory> alarm_factory,
    uint8_t expected_server_connection_id_length,
    QuicTransportSimpleServerSession::Mode mode,
    std::vector<url::Origin> accepted_origins)
    : QuicDispatcher(config,
                     crypto_config,
                     version_manager,
                     std::move(helper),
                     std::move(session_helper),
                     std::move(alarm_factory),
                     expected_server_connection_id_length),
      mode_(mode),
      accepted_origins_(accepted_origins) {}

QuicSession* QuicTransportSimpleServerDispatcher::CreateQuicSession(
    QuicConnectionId server_connection_id,
    const QuicSocketAddress& peer_address,
    QuicStringPiece /*alpn*/,
    const ParsedQuicVersion& version) {
  auto connection = std::make_unique<QuicConnection>(
      server_connection_id, peer_address, helper(), alarm_factory(), writer(),
      /*owns_writer=*/false, Perspective::IS_SERVER,
      ParsedQuicVersionVector{version});
  QuicTransportSimpleServerSession* session =
      new QuicTransportSimpleServerSession(
          connection.release(), /*owns_connection=*/true, this, config(),
          GetSupportedVersions(), crypto_config(), compressed_certs_cache(),
          mode_, accepted_origins_);
  session->Initialize();
  return session;
}

}  // namespace quic
