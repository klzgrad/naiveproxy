// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/test_tools/moqt_simulator_harness.h"

#include <string>

#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_generic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/simulator/simulator.h"
#include "quiche/quic/test_tools/simulator/test_harness.h"

namespace moqt::test {

namespace {
MoqtSessionParameters CreateParameters(quic::Perspective perspective,
                                       MoqtVersion version) {
  MoqtSessionParameters parameters(perspective, "");
  parameters.version = version;
  return parameters;
}
}  // namespace

MoqtClientEndpoint::MoqtClientEndpoint(quic::simulator::Simulator* simulator,
                                       const std::string& name,
                                       const std::string& peer_name,
                                       MoqtVersion version)
    : QuicEndpointWithConnection(simulator, name, peer_name,
                                 quic::Perspective::IS_CLIENT,
                                 quic::GetQuicVersionsForGenericSession()),
      crypto_config_(quic::test::crypto_test_utils::ProofVerifierForTesting()),
      quic_session_(connection_.get(), false, nullptr, quic::QuicConfig(),
                    "test.example.com", 443, "moqt", &session_,
                    /*visitor_owned=*/false, nullptr, &crypto_config_),
      session_(&quic_session_,
               CreateParameters(quic::Perspective::IS_CLIENT, version),
               MoqtSessionCallbacks()) {
  quic_session_.Initialize();
}

MoqtServerEndpoint::MoqtServerEndpoint(quic::simulator::Simulator* simulator,
                                       const std::string& name,
                                       const std::string& peer_name,
                                       MoqtVersion version)
    : QuicEndpointWithConnection(simulator, name, peer_name,
                                 quic::Perspective::IS_SERVER,
                                 quic::GetQuicVersionsForGenericSession()),
      compressed_certs_cache_(
          quic::QuicCompressedCertsCache::kQuicCompressedCertsCacheSize),
      crypto_config_(quic::QuicCryptoServerConfig::TESTING,
                     quic::QuicRandom::GetInstance(),
                     quic::test::crypto_test_utils::ProofSourceForTesting(),
                     quic::KeyExchangeSource::Default()),
      quic_session_(connection_.get(), false, nullptr, quic::QuicConfig(),
                    "moqt", &session_,
                    /*visitor_owned=*/false, nullptr, &crypto_config_,
                    &compressed_certs_cache_),
      session_(&quic_session_,
               CreateParameters(quic::Perspective::IS_SERVER, version),
               MoqtSessionCallbacks()) {
  quic_session_.Initialize();
}

}  // namespace moqt::test
