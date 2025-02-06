// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SIMULATOR_HARNESS_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SIMULATOR_HARNESS_H_

#include <string>

#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/quic_generic_session.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/test_tools/simulator/simulator.h"
#include "quiche/quic/test_tools/simulator/test_harness.h"

namespace moqt::test {

// Places a MoQT-over-raw-QUIC client within a network simulation.
class MoqtClientEndpoint : public quic::simulator::QuicEndpointWithConnection {
 public:
  MoqtClientEndpoint(quic::simulator::Simulator* simulator,
                     const std::string& name, const std::string& peer_name,
                     MoqtVersion version);

  MoqtSession* session() { return &session_; }
  quic::QuicGenericClientSession* quic_session() { return &quic_session_; }

 private:
  quic::QuicCryptoClientConfig crypto_config_;
  quic::QuicGenericClientSession quic_session_;
  MoqtSession session_;
};

// Places a MoQT-over-raw-QUIC server within a network simulation.
class MoqtServerEndpoint : public quic::simulator::QuicEndpointWithConnection {
 public:
  MoqtServerEndpoint(quic::simulator::Simulator* simulator,
                     const std::string& name, const std::string& peer_name,
                     MoqtVersion version);

  MoqtSession* session() { return &session_; }
  quic::QuicGenericServerSession* quic_session() { return &quic_session_; }

 private:
  quic::QuicCompressedCertsCache compressed_certs_cache_;
  quic::QuicCryptoServerConfig crypto_config_;
  quic::QuicGenericServerSession quic_session_;
  MoqtSession session_;
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SIMULATOR_HARNESS_H_
