// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_CLIENT_FACTORY_H_
#define QUICHE_QUIC_TOOLS_QUIC_CLIENT_FACTORY_H_

#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/tools/quic_spdy_client_base.h"

namespace quic {

// Interface implemented by Factories to create QuicClients.
class ClientFactoryInterface {
 public:
  virtual ~ClientFactoryInterface() = default;

  // Creates a new client configured to connect to |host_for_lookup:port|
  // supporting |versions|, using |host_for_handshake| for handshake and
  // |verifier| to verify proofs.
  virtual std::unique_ptr<QuicSpdyClientBase> CreateClient(
      std::string host_for_handshake, std::string host_for_lookup,
      // AF_INET, AF_INET6, or AF_UNSPEC(=don't care).
      int address_family_for_lookup, uint16_t port,
      ParsedQuicVersionVector versions, const QuicConfig& config,
      std::unique_ptr<ProofVerifier> verifier,
      std::unique_ptr<SessionCache> session_cache) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_CLIENT_FACTORY_H_
