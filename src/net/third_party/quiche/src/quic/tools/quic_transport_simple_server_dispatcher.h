// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_TRANSPORT_SIMPLE_SERVER_DISPATCHER_H_
#define QUICHE_QUIC_TOOLS_QUIC_TRANSPORT_SIMPLE_SERVER_DISPATCHER_H_

#include "url/origin.h"
#include "net/third_party/quiche/src/quic/core/quic_dispatcher.h"
#include "net/third_party/quiche/src/quic/tools/quic_transport_simple_server_session.h"

namespace quic {

// Dispatcher that creates a QuicTransportSimpleServerSession for every incoming
// connection.
class QuicTransportSimpleServerDispatcher : public QuicDispatcher {
 public:
  QuicTransportSimpleServerDispatcher(
      const QuicConfig* config,
      const QuicCryptoServerConfig* crypto_config,
      QuicVersionManager* version_manager,
      std::unique_ptr<QuicConnectionHelperInterface> helper,
      std::unique_ptr<QuicCryptoServerStream::Helper> session_helper,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      uint8_t expected_server_connection_id_length,
      QuicTransportSimpleServerSession::Mode mode,
      std::vector<url::Origin> accepted_origins);

 protected:
  QuicSession* CreateQuicSession(QuicConnectionId server_connection_id,
                                 const QuicSocketAddress& peer_address,
                                 QuicStringPiece alpn,
                                 const ParsedQuicVersion& version) override;

  QuicTransportSimpleServerSession::Mode mode_;
  std::vector<url::Origin> accepted_origins_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_TRANSPORT_SIMPLE_SERVER_DISPATCHER_H_
