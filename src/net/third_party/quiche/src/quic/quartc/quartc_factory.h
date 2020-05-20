// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_QUARTC_FACTORY_H_
#define QUICHE_QUIC_QUARTC_QUARTC_FACTORY_H_

#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_packet_writer.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_session.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

struct QuartcSessionConfig {
  // If a pre-shared cryptographic key is available for this session, specify it
  // here.  This value will only be used if non-empty.
  std::string pre_shared_key;

  // The maximum size of the packet can be written with the packet writer.
  // 1200 bytes by default.
  QuicPacketLength max_packet_size = 1200;

  // Timeouts for the crypto handshake. Set them to higher values to
  // prevent closing the session before it started on a slow network.
  // Zero entries are ignored and QUIC defaults are used in that case.
  QuicTime::Delta max_idle_time_before_crypto_handshake =
      QuicTime::Delta::Zero();
  QuicTime::Delta max_time_before_crypto_handshake = QuicTime::Delta::Zero();
  QuicTime::Delta idle_network_timeout = QuicTime::Delta::Zero();

  // Tail loss probes (TLP) are enabled by default, but it may be useful to
  // disable them in tests. We can also consider disabling them in production
  // if we discover that tail loss probes add overhead in low bitrate audio.
  bool enable_tail_loss_probe = true;
};

// Creates a new QuartcClientSession using the given configuration.
std::unique_ptr<QuartcSession> CreateQuartcClientSession(
    const QuartcSessionConfig& quartc_session_config,
    const QuicClock* clock,
    QuicAlarmFactory* alarm_factory,
    QuicConnectionHelperInterface* connection_helper,
    const ParsedQuicVersionVector& supported_versions,
    quiche::QuicheStringPiece server_crypto_config,
    QuartcPacketTransport* packet_transport);

// Configures global settings, such as supported quic versions.
// Must execute on QUIC thread.
void ConfigureGlobalQuicSettings();

// Must execute on QUIC thread.
QuicConfig CreateQuicConfig(const QuartcSessionConfig& quartc_session_config);

std::unique_ptr<QuicConnection> CreateQuicConnection(
    QuicConnectionId connection_id,
    const QuicSocketAddress& peer_address,
    QuicConnectionHelperInterface* connection_helper,
    QuicAlarmFactory* alarm_factory,
    QuicPacketWriter* packet_writer,
    Perspective perspective,
    ParsedQuicVersionVector supported_versions);

}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_QUARTC_FACTORY_H_
