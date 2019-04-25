// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_FACTORY_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_FACTORY_H_

#include "net/third_party/quic/core/quic_alarm_factory.h"
#include "net/third_party/quic/core/quic_connection.h"
#include "net/third_party/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quic/quartc/quartc_packet_writer.h"
#include "net/third_party/quic/quartc/quartc_session.h"

namespace quic {

// The configuration for creating a QuartcFactory.
struct QuartcFactoryConfig {
  // Factory for |QuicAlarm|s. Implemented by the Quartc user with different
  // mechanisms. For example in WebRTC, it is implemented with rtc::Thread.
  // Owned by the user, and needs to stay alive for as long as the QuartcFactory
  // exists.
  QuicAlarmFactory* alarm_factory = nullptr;
  // The clock used by |QuicAlarm|s. Implemented by the Quartc user. Owned by
  // the user, and needs to stay alive for as long as the QuartcFactory exists.
  const QuicClock* clock = nullptr;
};

struct QuartcSessionConfig {
  // If a pre-shared cryptographic key is available for this session, specify it
  // here.  This value will only be used if non-empty.
  QuicString pre_shared_key;

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

// Factory that creates instances of QuartcSession.  Implements the
// QuicConnectionHelperInterface used by the QuicConnections. Only one
// QuartcFactory is expected to be created.
class QuartcFactory {
 public:
  explicit QuartcFactory(const QuartcFactoryConfig& factory_config);

  // Creates a new QuartcSession using the given configuration.
  std::unique_ptr<QuartcSession> CreateQuartcClientSession(
      const QuartcSessionConfig& quartc_session_config,
      const ParsedQuicVersionVector& supported_versions,
      QuicStringPiece server_crypto_config,
      QuartcPacketTransport* packet_transport);

 private:
  std::unique_ptr<QuicConnection> CreateQuicConnection(
      Perspective perspective,
      const ParsedQuicVersionVector& supported_versions,
      QuartcPacketWriter* packet_writer);

  // Used to implement QuicAlarmFactory.  Owned by the user and must outlive
  // QuartcFactory.
  QuicAlarmFactory* alarm_factory_;
  // Used to implement the QuicConnectionHelperInterface.  Owned by the user and
  // must outlive QuartcFactory.
  const QuicClock* clock_;

  // Helper used by all QuicConnections.
  std::unique_ptr<QuicConnectionHelperInterface> connection_helper_;

  // Used by QuicCryptoServerStream to track most recently compressed certs.
  std::unique_ptr<QuicCompressedCertsCache> compressed_certs_cache_;

  // This helper is needed to create QuicCryptoServerStreams.
  std::unique_ptr<QuicCryptoServerStream::Helper> stream_helper_;
};

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

// Creates a new instance of QuartcFactory.
std::unique_ptr<QuartcFactory> CreateQuartcFactory(
    const QuartcFactoryConfig& factory_config);

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_FACTORY_H_
