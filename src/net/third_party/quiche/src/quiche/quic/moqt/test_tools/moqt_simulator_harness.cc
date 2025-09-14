// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/test_tools/moqt_simulator_harness.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_alarm_factory_proxy.h"
#include "quiche/quic/core/quic_generic_session.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/simulator/simulator.h"
#include "quiche/quic/test_tools/simulator/test_harness.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace moqt::test {

namespace {
MoqtSessionParameters CreateParameters(quic::Perspective perspective,
                                       MoqtVersion version) {
  MoqtSessionParameters parameters(perspective, "");
  parameters.version = version;
  parameters.deliver_partial_objects = false;
  return parameters;
}

MoqtSessionCallbacks CreateCallbacks(quic::simulator::Simulator* simulator) {
  return MoqtSessionCallbacks(
      +[] {}, +[](absl::string_view) {}, +[](absl::string_view) {}, +[] {},
      DefaultIncomingAnnounceCallback,
      DefaultIncomingSubscribeAnnouncesCallback, simulator->GetClock());
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
               std::make_unique<quic::QuicAlarmFactoryProxy>(
                   simulator->GetAlarmFactory()),
               CreateCallbacks(simulator)) {
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
               std::make_unique<quic::QuicAlarmFactoryProxy>(
                   simulator->GetAlarmFactory()),
               CreateCallbacks(simulator)) {
  quic_session_.Initialize();
}

void RunHandshakeOrDie(quic::simulator::Simulator& simulator,
                       MoqtClientEndpoint& client, MoqtServerEndpoint& server,
                       std::optional<quic::QuicTimeDelta> timeout) {
  constexpr quic::QuicTimeDelta kDefaultTimeout =
      quic::QuicTimeDelta::FromSeconds(3);
  bool client_established = false;
  bool server_established = false;
  MoqtSessionEstablishedCallback old_client_callback =
      std::move(client.session()->callbacks().session_established_callback);
  MoqtSessionEstablishedCallback old_server_callback =
      std::move(server.session()->callbacks().session_established_callback);

  // Retaining pointers to local variables is safe here, since if the handshake
  // succeeds, both callbacks are executed and deleted, and if either fails, the
  // program crashes.
  client.session()->callbacks().session_established_callback =
      [&client_established] { client_established = true; };
  server.session()->callbacks().session_established_callback =
      [&server_established] { server_established = true; };

  client.quic_session()->CryptoConnect();
  simulator.RunUntilOrTimeout(
      [&]() { return client_established && server_established; },
      timeout.value_or(kDefaultTimeout));
  QUICHE_CHECK(client_established) << "Client failed to establish session";
  QUICHE_CHECK(server_established) << "Server failed to establish session";
  std::move(old_client_callback)();
  std::move(old_server_callback)();
}

}  // namespace moqt::test
