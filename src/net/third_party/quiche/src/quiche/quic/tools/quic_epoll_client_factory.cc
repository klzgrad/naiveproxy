// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_epoll_client_factory.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_default_client.h"
#include "quiche/quic/tools/quic_name_lookup.h"

namespace quic {

QuicEpollClientFactory::QuicEpollClientFactory()
    : event_loop_(GetDefaultEventLoop()->Create(QuicDefaultClock::Get())) {}

std::unique_ptr<QuicSpdyClientBase> QuicEpollClientFactory::CreateClient(
    std::string host_for_handshake, std::string host_for_lookup,
    int address_family_for_lookup, uint16_t port,
    ParsedQuicVersionVector versions, const QuicConfig& config,
    std::unique_ptr<ProofVerifier> verifier,
    std::unique_ptr<SessionCache> session_cache) {
  QuicSocketAddress addr = tools::LookupAddress(
      address_family_for_lookup, host_for_lookup, absl::StrCat(port));
  if (!addr.IsInitialized()) {
    QUIC_LOG(ERROR) << "Unable to resolve address: " << host_for_lookup;
    return nullptr;
  }
  QuicServerId server_id(host_for_handshake, port, false);
  return std::make_unique<QuicDefaultClient>(
      addr, server_id, versions, config, event_loop_.get(), std::move(verifier),
      std::move(session_cache));
}

}  // namespace quic
