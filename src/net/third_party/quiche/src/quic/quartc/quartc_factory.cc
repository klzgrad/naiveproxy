// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_factory.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/tls_client_handshaker.h"
#include "net/third_party/quiche/src/quic/core/tls_server_handshaker.h"
#include "net/third_party/quiche/src/quic/core/uber_received_packet_manager.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_connection_helper.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_crypto_helpers.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_session.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

std::unique_ptr<QuartcSession> CreateQuartcClientSession(
    const QuartcSessionConfig& quartc_session_config,
    const QuicClock* clock,
    QuicAlarmFactory* alarm_factory,
    QuicConnectionHelperInterface* connection_helper,
    const ParsedQuicVersionVector& supported_versions,
    quiche::QuicheStringPiece server_crypto_config,
    QuartcPacketTransport* packet_transport) {
  DCHECK(packet_transport);

  // QuartcSession will eventually own both |writer| and |quic_connection|.
  auto writer = std::make_unique<QuartcPacketWriter>(
      packet_transport, quartc_session_config.max_packet_size);

  // While the QuicConfig is not directly used by the connection, creating it
  // also sets flag values which must be set before creating the connection.
  QuicConfig quic_config = CreateQuicConfig(quartc_session_config);

  // |dummy_id| and |dummy_address| are used because Quartc network layer will
  // not use these two.
  QuicConnectionId dummy_id = QuicUtils::CreateZeroConnectionId(
      supported_versions[0].transport_version);
  QuicSocketAddress dummy_address(QuicIpAddress::Any4(), /*port=*/0);
  std::unique_ptr<QuicConnection> quic_connection = CreateQuicConnection(
      dummy_id, dummy_address, connection_helper, alarm_factory, writer.get(),
      Perspective::IS_CLIENT, supported_versions);

  // Quartc sets its own ack delay; get that ack delay and copy it over
  // to the QuicConfig so that it can be properly advertised to the peer
  // via transport parameter negotiation.
  quic_config.SetMaxAckDelayToSendMs(quic_connection->received_packet_manager()
                                         .max_ack_delay()
                                         .ToMilliseconds());

  return std::make_unique<QuartcClientSession>(
      std::move(quic_connection), quic_config, supported_versions, clock,
      std::move(writer),
      CreateCryptoClientConfig(quartc_session_config.pre_shared_key),
      server_crypto_config);
}

void ConfigureGlobalQuicSettings() {
  // Ensure that we don't drop data because QUIC streams refuse to buffer it.
  // TODO(b/120099046):  Replace this with correct handling of WriteMemSlices().
  SetQuicFlag(FLAGS_quic_buffered_data_threshold,
              std::numeric_limits<int>::max());

  // Enable and request QUIC to include receive timestamps in ACK frames.
  SetQuicReloadableFlag(quic_send_timestamps, true);

  // Enable ACK_DECIMATION_WITH_REORDERING. It requires ack_decimation to be
  // false.
  SetQuicReloadableFlag(quic_enable_ack_decimation, false);

  // Note: flag settings have no effect for Exoblaze builds since
  // SetQuicReloadableFlag() gets stubbed out.
  SetQuicReloadableFlag(quic_unified_iw_options, true);  // Enable IWXX opts.
  SetQuicReloadableFlag(quic_bbr_flexible_app_limited, true);  // Enable BBR9.
}

QuicConfig CreateQuicConfig(const QuartcSessionConfig& quartc_session_config) {
  // TODO(b/124398962): Figure out a better way to initialize QUIC flags.
  // Creating a config shouldn't have global side-effects on flags.  However,
  // this has the advantage of ensuring that flag values stay in sync with the
  // options requested by configs, so simply splitting the config and flag
  // settings doesn't seem preferable.
  ConfigureGlobalQuicSettings();

  QuicTagVector copt;
  copt.push_back(kNSTP);

  // Enable and request QUIC to include receive timestamps in ACK frames.
  copt.push_back(kSTMP);

  // Enable ACK_DECIMATION_WITH_REORDERING. It requires ack_decimation to be
  // false.
  copt.push_back(kAKD2);

  // Use unlimited decimation in order to reduce number of unbundled ACKs.
  copt.push_back(kAKDU);

  // Enable time-based loss detection.
  copt.push_back(kTIME);

  copt.push_back(kBBR3);  // Stay in low-gain until in-flight < BDP.
  copt.push_back(kBBR5);  // 40 RTT ack aggregation.
  copt.push_back(kBBR9);  // Ignore app-limited if enough data is in flight.
  copt.push_back(kBBQ1);  // 2.773 pacing gain in STARTUP.
  copt.push_back(kBBQ2);  // 2.0 CWND gain in STARTUP.
  copt.push_back(k1RTT);  // Exit STARTUP after 1 RTT with no gains.
  copt.push_back(kIW10);  // 10-packet (14600 byte) initial cwnd.

  if (!quartc_session_config.enable_tail_loss_probe) {
    copt.push_back(kNTLP);
  }

  // TODO(b/112192153):  Test and possible enable slower startup when pipe
  // filling is ready to use.  Slower startup is kBBRS.

  QuicConfig quic_config;

  // Use the limits for the session & stream flow control. The default 16KB
  // limit leads to significantly undersending (not reaching BWE on the outgoing
  // bitrate) due to blocked frames, and it leads to high latency (and one-way
  // delay). Setting it to its limits is not going to cause issues (our streams
  // are small generally, and if we were to buffer 24MB it wouldn't be the end
  // of the world). We can consider setting different limits in future (e.g. 1MB
  // stream, 1.5MB session). It's worth noting that on 1mbps bitrate, limit of
  // 24MB can capture approx 4 minutes of the call, and the default increase in
  // size of the window (half of the window size) is approximately 2 minutes of
  // the call.
  quic_config.SetInitialSessionFlowControlWindowToSend(
      kSessionReceiveWindowLimit);
  quic_config.SetInitialStreamFlowControlWindowToSend(
      kStreamReceiveWindowLimit);
  quic_config.SetConnectionOptionsToSend(copt);
  quic_config.SetClientConnectionOptions(copt);
  if (quartc_session_config.max_time_before_crypto_handshake >
      QuicTime::Delta::Zero()) {
    quic_config.set_max_time_before_crypto_handshake(
        quartc_session_config.max_time_before_crypto_handshake);
  }
  if (quartc_session_config.max_idle_time_before_crypto_handshake >
      QuicTime::Delta::Zero()) {
    quic_config.set_max_idle_time_before_crypto_handshake(
        quartc_session_config.max_idle_time_before_crypto_handshake);
  }
  if (quartc_session_config.idle_network_timeout > QuicTime::Delta::Zero()) {
    quic_config.SetIdleNetworkTimeout(
        quartc_session_config.idle_network_timeout,
        quartc_session_config.idle_network_timeout);
  }

  // The ICE transport provides a unique 5-tuple for each connection. Save
  // overhead by omitting the connection id.
  quic_config.SetBytesForConnectionIdToSend(0);

  // Allow up to 1000 incoming streams at once. Quartc streams typically contain
  // one audio or video frame and close immediately. However, when a video frame
  // becomes larger than one packet, there is some delay between the start and
  // end of each stream. The default maximum of 100 only leaves about 1 second
  // of headroom (Quartc sends ~30 video frames per second) before QUIC starts
  // to refuse incoming streams. Back-pressure should clear backlogs of
  // incomplete streams, but targets 1 second for recovery. Increasing the
  // number of open streams gives sufficient headroom to recover before QUIC
  // refuses new streams.
  quic_config.SetMaxBidirectionalStreamsToSend(1000);

  return quic_config;
}

std::unique_ptr<QuicConnection> CreateQuicConnection(
    QuicConnectionId connection_id,
    const QuicSocketAddress& peer_address,
    QuicConnectionHelperInterface* connection_helper,
    QuicAlarmFactory* alarm_factory,
    QuicPacketWriter* packet_writer,
    Perspective perspective,
    ParsedQuicVersionVector supported_versions) {
  auto quic_connection = std::make_unique<QuicConnection>(
      connection_id, peer_address, connection_helper, alarm_factory,
      packet_writer,
      /*owns_writer=*/false, perspective, supported_versions);
  quic_connection->SetMaxPacketLength(
      packet_writer->GetMaxPacketSize(peer_address));

  QuicSentPacketManager& sent_packet_manager =
      quic_connection->sent_packet_manager();
  UberReceivedPacketManager& received_packet_manager =
      quic_connection->received_packet_manager();

  // Default delayed ack time is 25ms.
  // If data packets are sent less often (e.g. because p-time was modified),
  // we would force acks to be sent every 25ms regardless, increasing
  // overhead. Since generally we guarantee a packet every 20ms, changing
  // this value should have miniscule effect on quality on good connections,
  // but on poor connections, changing this number significantly reduced the
  // number of ack-only packets.
  // The p-time can go up to as high as 120ms, and when it does, it's
  // when the low overhead is the most important thing. Ideally it should be
  // above 120ms, but it cannot be higher than 0.5*RTO, which equals to 100ms.
  received_packet_manager.set_max_ack_delay(
      QuicTime::Delta::FromMilliseconds(100));
  sent_packet_manager.set_peer_max_ack_delay(
      QuicTime::Delta::FromMilliseconds(100));

  quic_connection->set_fill_up_link_during_probing(true);

  // We start ack decimation after 15 packets. Typically, we would see
  // 1-2 crypto handshake packets, one media packet, and 10 probing packets.
  // We want to get acks for the probing packets as soon as possible,
  // but we can start using ack decimation right after first probing completes.
  // The default was to not start ack decimation for the first 100 packets.
  quic_connection->set_min_received_before_ack_decimation(15);

  return quic_connection;
}

}  // namespace quic
