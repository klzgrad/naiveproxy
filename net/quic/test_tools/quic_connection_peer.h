// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_

#include "base/macros.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_connection_stats.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

struct QuicPacketHeader;
class QuicAlarm;
class QuicConnectionHelperInterface;
class QuicConnectionVisitorInterface;
class QuicEncryptedPacket;
class QuicFramer;
class QuicPacketCreator;
class QuicPacketGenerator;
class QuicPacketWriter;
class QuicSentPacketManager;
class SendAlgorithmInterface;

namespace test {

// Peer to make public a number of otherwise private QuicConnection methods.
class QuicConnectionPeer {
 public:
  static void SendAck(QuicConnection* connection);

  static void SetSendAlgorithm(QuicConnection* connection,
                               SendAlgorithmInterface* send_algorithm);

  static void SetLossAlgorithm(QuicConnection* connection,
                               LossDetectionInterface* loss_algorithm);

  static const QuicFrame GetUpdatedAckFrame(QuicConnection* connection);

  static void PopulateStopWaitingFrame(QuicConnection* connection,
                                       QuicStopWaitingFrame* stop_waiting);

  static QuicConnectionVisitorInterface* GetVisitor(QuicConnection* connection);

  static QuicPacketCreator* GetPacketCreator(QuicConnection* connection);

  static QuicPacketGenerator* GetPacketGenerator(QuicConnection* connection);

  static QuicSentPacketManager* GetSentPacketManager(
      QuicConnection* connection);

  static QuicTime::Delta GetNetworkTimeout(QuicConnection* connection);

  static void SetPerspective(QuicConnection* connection,
                             Perspective perspective);

  static void SetSelfAddress(QuicConnection* connection,
                             const QuicSocketAddress& self_address);

  static void SetPeerAddress(QuicConnection* connection,
                             const QuicSocketAddress& peer_address);

  static bool IsSilentCloseEnabled(QuicConnection* connection);

  static void SwapCrypters(QuicConnection* connection, QuicFramer* framer);

  static void SetCurrentPacket(QuicConnection* connection,
                               QuicStringPiece current_packet);

  static QuicConnectionHelperInterface* GetHelper(QuicConnection* connection);

  static QuicAlarmFactory* GetAlarmFactory(QuicConnection* connection);

  static QuicFramer* GetFramer(QuicConnection* connection);

  static QuicAlarm* GetAckAlarm(QuicConnection* connection);
  static QuicAlarm* GetPingAlarm(QuicConnection* connection);
  static QuicAlarm* GetResumeWritesAlarm(QuicConnection* connection);
  static QuicAlarm* GetRetransmissionAlarm(QuicConnection* connection);
  static QuicAlarm* GetSendAlarm(QuicConnection* connection);
  static QuicAlarm* GetTimeoutAlarm(QuicConnection* connection);
  static QuicAlarm* GetMtuDiscoveryAlarm(QuicConnection* connection);

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

  static void SetPacketsBetweenMtuProbes(QuicConnection* connection,
                                         QuicPacketCount packets);
  static void SetNextMtuProbeAt(QuicConnection* connection,
                                QuicPacketNumber number);
  static void SetAckMode(QuicConnection* connection,
                         QuicConnection::AckMode ack_mode);
  static void SetAckDecimationDelay(QuicConnection* connection,
                                    float ack_decimation_delay);
  static bool HasRetransmittableFrames(QuicConnection* connection,
                                       QuicPacketNumber packet_number);
  static void SetNoStopWaitingFrames(QuicConnection* connection,
                                     bool no_stop_waiting_frames);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicConnectionPeer);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_
