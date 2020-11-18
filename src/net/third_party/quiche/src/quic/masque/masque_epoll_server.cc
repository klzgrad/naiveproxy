// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_epoll_server.h"
#include "net/third_party/quiche/src/quic/core/quic_epoll_alarm_factory.h"
#include "net/third_party/quiche/src/quic/masque/masque_dispatcher.h"
#include "net/third_party/quiche/src/quic/masque/masque_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_default_proof_providers.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_crypto_server_stream_helper.h"

namespace quic {

MasqueEpollServer::MasqueEpollServer(MasqueServerBackend* masque_server_backend)
    : QuicServer(CreateDefaultProofSource(),
                 masque_server_backend,
                 MasqueSupportedVersions()),
      masque_server_backend_(masque_server_backend) {}

QuicDispatcher* MasqueEpollServer::CreateQuicDispatcher() {
  QuicEpollAlarmFactory alarm_factory(epoll_server());
  return new MasqueDispatcher(
      &config(), &crypto_config(), version_manager(),
      std::make_unique<QuicEpollConnectionHelper>(epoll_server(),
                                                  QuicAllocator::BUFFER_POOL),
      std::make_unique<QuicSimpleCryptoServerStreamHelper>(),
      std::make_unique<QuicEpollAlarmFactory>(epoll_server()),
      masque_server_backend_, expected_server_connection_id_length());
}

}  // namespace quic
