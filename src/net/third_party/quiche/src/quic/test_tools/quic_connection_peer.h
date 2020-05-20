// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_

#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

struct QuicPacketHeader;
class QuicAlarm;
class QuicConnectionHelperInterface;
class QuicConnectionVisitorInterface;
class QuicEncryptedPacket;
class QuicFramer;
class QuicPacketCreator;
class QuicPacketWriter;
class QuicSentPacketManager;
class SendAlgorithmInterface;

namespace test {

// Peer to make public a number of otherwise private QuicConnection methods.
class QuicConnectionPeer {
 public:
  QuicConnectionPeer() = delete;

  static void SetSendAlgorithm(QuicConnection* connection,
                               SendAlgorithmInterface* send_algorithm);

  static void SetLossAlgorithm(QuicConnection* connection,
                               LossDetectionInterface* loss_algorithm);

  static void PopulateStopWaitingFrame(QuicConnection* connection,
                                       QuicStopWaitingFrame* stop_waiting);

  static QuicPacketCreator* GetPacketCreator(QuicConnection* connection);

  static QuicSentPacketManager* GetSentPacketManager(
      QuicConnection* connection);

  static QuicTime::Delta GetNetworkTimeout(QuicConnection* connection);

  static void SetPerspective(QuicConnection* connection,
                             Perspective perspective);

  static void SetSelfAddress(QuicConnection* connection,
                             const QuicSocketAddress& self_address);

  static void SetPeerAddress(QuicConnection* connection,
                             const QuicSocketAddress& peer_address);

  static void SetDirectPeerAddress(
      QuicConnection* connection,
      const QuicSocketAddress& direct_peer_address);

  static void SetEffectivePeerAddress(
      QuicConnection* connection,
      const QuicSocketAddress& effective_peer_address);

  static bool IsSilentCloseEnabled(QuicConnection* connection);

  static void SwapCrypters(QuicConnection* connection, QuicFramer* framer);

  static void SetCurrentPacket(QuicConnection* connection,
                               quiche::QuicheStringPiece current_packet);

  static QuicConnectionHelperInterface* GetHelper(QuicConnection* connection);

  static QuicAlarmFactory* GetAlarmFactory(QuicConnection* connection);

  static QuicFramer* GetFramer(QuicConnection* connection);

  static QuicAlarm* GetAckAlarm(QuicConnection* connection);
  static QuicAlarm* GetPingAlarm(QuicConnection* connection);
  static QuicAlarm* GetRetransmissionAlarm(QuicConnection* connection);
  static QuicAlarm* GetSendAlarm(QuicConnection* connection);
  static QuicAlarm* GetTimeoutAlarm(QuicConnection* connection);
  static QuicAlarm* GetMtuDiscoveryAlarm(QuicConnection* connection);
  static QuicAlarm* GetPathDegradingAlarm(QuicConnection* connection);
  static QuicAlarm* GetProcessUndecryptablePacketsAlarm(
      QuicConnection* connection);

  static QuicPacketWriter* GetWriter(QuicConnection* connection);
  // If |owns_writer| is true, takes ownership of |writer|.
  static void SetWriter(QuicConnection* connection,
                        QuicPacketWriter* writer,
                        bool owns_writer);
  static void TearDownLocalConnectionState(QuicConnection* connection);
  static QuicEncryptedPacket* GetConnectionClosePacket(
      QuicConnection* connection);

  static QuicPacketHeader* GetLastHeader(QuicConnection* connection);

  static QuicConnectionStats* GetStats(QuicConnection* connection);

  static QuicPacketCount GetPacketsBetweenMtuProbes(QuicConnection* connection);

  static void ReInitializeMtuDiscoverer(
      QuicConnection* connection,
      QuicPacketCount packets_between_probes_base,
      QuicPacketNumber next_probe_at);
  static void SetAckMode(QuicConnection* connection, AckMode ack_mode);
  static void SetFastAckAfterQuiescence(QuicConnection* connection,
                                        bool fast_ack_after_quiescence);
  static void SetAckDecimationDelay(QuicConnection* connection,
                                    float ack_decimation_delay);
  static bool HasRetransmittableFrames(QuicConnection* connection,
                                       uint64_t packet_number);
  static bool GetNoStopWaitingFrames(QuicConnection* connection);
  static void SetNoStopWaitingFrames(QuicConnection* connection,
                                     bool no_stop_waiting_frames);
  static void SetMaxTrackedPackets(QuicConnection* connection,
                                   QuicPacketCount max_tracked_packets);
  static void SetNegotiatedVersion(QuicConnection* connection);
  static void SetMaxConsecutiveNumPacketsWithNoRetransmittableFrames(
      QuicConnection* connection,
      size_t new_value);
  static bool SupportsReleaseTime(QuicConnection* connection);
  static QuicConnection::PacketContent GetCurrentPacketContent(
      QuicConnection* connection);
  static void SetLastHeaderFormat(QuicConnection* connection,
                                  PacketHeaderFormat format);
  static void AddBytesReceived(QuicConnection* connection, size_t length);
  static void SetAddressValidated(QuicConnection* connection);

  static void SendConnectionClosePacket(QuicConnection* connection,
                                        QuicErrorCode error,
                                        const std::string& details);

  static size_t GetNumEncryptionLevels(QuicConnection* connection);

  static QuicNetworkBlackholeDetector& GetBlackholeDetector(
      QuicConnection* connection);

  static QuicAlarm* GetBlackholeDetectorAlarm(QuicConnection* connection);

  static QuicTime GetPathDegradingDeadline(QuicConnection* connection);

  static QuicTime GetBlackholeDetectionDeadline(QuicConnection* connection);

  static QuicAlarm* GetIdleNetworkDetectorAlarm(QuicConnection* connection);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_
