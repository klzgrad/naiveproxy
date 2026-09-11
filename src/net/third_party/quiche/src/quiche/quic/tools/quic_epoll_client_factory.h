// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_EPOLL_CLIENT_FACTORY_H_
#define QUICHE_QUIC_TOOLS_QUIC_EPOLL_CLIENT_FACTORY_H_

#include <memory>

#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/tools/quic_client_factory.h"

namespace quic {

// Factory creating QuicClient instances.
class QuicEpollClientFactory : public ClientFactoryInterface {
 public:
  QuicEpollClientFactory();

  std::unique_ptr<QuicSpdyClientBase> CreateClient(
      std::string host_for_handshake, std::string host_for_lookup,
      int address_family_for_lookup, uint16_t port,
      ParsedQuicVersionVector versions, const QuicConfig& config,
      std::unique_ptr<ProofVerifier> verifier,
      std::unique_ptr<SessionCache> session_cache) override;

 private:
  std::unique_ptr<QuicEventLoop> event_loop_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_EPOLL_CLIENT_FACTORY_H_
