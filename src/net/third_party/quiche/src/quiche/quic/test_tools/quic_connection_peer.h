// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_connection_stats.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

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

  static QuicPacketCreator* GetPacketCreator(QuicConnection* connection);

  static QuicSentPacketManager* GetSentPacketManager(
      QuicConnection* connection);

  static QuicTime::Delta GetNetworkTimeout(QuicConnection* connection);

  static QuicTime::Delta GetHandshakeTimeout(QuicConnection* connection);

  static void SetPerspective(QuicConnection* connection,
                             Perspective perspective);

  static void SetSelfAddress(QuicConnection* connection,
                             const QuicSocketAddress& self_address);

  static void SetPeerAddress(QuicConnection* connection,
                             const QuicSocketAddress& peer_address);

  static void SetDirectPeerAddress(
      QuicConnection* connection, const QuicSocketAddress& direct_peer_address);

  static void SetEffectivePeerAddress(
      QuicConnection* connection,
      const QuicSocketAddress& effective_peer_address);

  static void SwapCrypters(QuicConnection* connection, QuicFramer* framer);

  static void SetCurrentPacket(QuicConnection* connection,
                               absl::string_view current_packet);

  static QuicConnectionHelperInterface* GetHelper(QuicConnection* connection);

  static QuicAlarmFactory* GetAlarmFactory(QuicConnection* connection);

  static QuicFramer* GetFramer(QuicConnection* connection);

  static QuicAlarm* GetAckAlarm(QuicConnection* connection);
  static QuicAlarm* GetPingAlarm(QuicConnection* connection);
  static QuicAlarm* GetRetransmissionAlarm(QuicConnection* connection);
  static QuicAlarm* GetSendAlarm(QuicConnection* connection);
  static QuicAlarm* GetMtuDiscoveryAlarm(QuicConnection* connection);
  static QuicAlarm* GetProcessUndecryptablePacketsAlarm(
      QuicConnection* connection);
  static QuicAlarm* GetDiscardPreviousOneRttKeysAlarm(
      QuicConnection* connection);
  static QuicAlarm* GetDiscardZeroRttDecryptionKeysAlarm(
      QuicConnection* connection);
  static QuicAlarm* GetRetirePeerIssuedConnectionIdAlarm(
      QuicConnection* connection);
  static QuicAlarm* GetRetireSelfIssuedConnectionIdAlarm(
      QuicConnection* connection);

  static QuicPacketWriter* GetWriter(QuicConnection* connection);
  // If |owns_writer| is true, takes ownership of |writer|.
  static void SetWriter(QuicConnection* connection, QuicPacketWriter* writer,
                        bool owns_writer);
  static void TearDownLocalConnectionState(QuicConnection* connection);
  static QuicEncryptedPacket* GetConnectionClosePacket(
      QuicConnection* connection);

  static QuicPacketHeader* GetLastHeader(QuicConnection* connection);

  static QuicConnectionStats* GetStats(QuicConnection* connection);

  static QuicPacketCount GetPacketsBetweenMtuProbes(QuicConnection* connection);

  static void ReInitializeMtuDiscoverer(
      QuicConnection* connection, QuicPacketCount packets_between_probes_base,
      QuicPacketNumber next_probe_at);
  static void SetAckDecimationDelay(QuicConnection* connection,
                                    float ack_decimation_delay);
  static bool HasRetransmittableFrames(QuicConnection* connection,
                                       uint64_t packet_number);
  static void SetMaxTrackedPackets(QuicConnection* connection,
                                   QuicPacketCount max_tracked_packets);
  static void SetNegotiatedVersion(QuicConnection* connection);
  static void SetMaxConsecutiveNumPacketsWithNoRetransmittableFrames(
      QuicConnection* connection, size_t new_value);
  static bool SupportsReleaseTime(QuicConnection* connection);
  static QuicConnection::PacketContent GetCurrentPacketContent(
      QuicConnection* connection);
  static void AddBytesReceived(QuicConnection* connection, size_t length);
  static void SetAddressValidated(QuicConnection* connection);

  static void SendConnectionClosePacket(QuicConnection* connection,
                                        QuicIetfTransportErrorCodes ietf_error,
                                        QuicErrorCode error,
                                        const std::string& details);

  static size_t GetNumEncryptionLevels(QuicConnection* connection);

  static QuicNetworkBlackholeDetector& GetBlackholeDetector(
      QuicConnection* connection);

  static QuicAlarm* GetBlackholeDetectorAlarm(QuicConnection* connection);

  static QuicTime GetPathDegradingDeadline(QuicConnection* connection);

  static QuicTime GetBlackholeDetectionDeadline(QuicConnection* connection);

  static QuicTime GetPathMtuReductionDetectionDeadline(
      QuicConnection* connection);

  static QuicAlarm* GetIdleNetworkDetectorAlarm(QuicConnection* connection);

  static QuicTime GetIdleNetworkDeadline(QuicConnection* connection);

  static QuicIdleNetworkDetector& GetIdleNetworkDetector(
      QuicConnection* connection);

  static void SetServerConnectionId(
      QuicConnection* connection, const QuicConnectionId& server_connection_id);

  static size_t NumUndecryptablePackets(QuicConnection* connection);

  static void SetConnectionClose(QuicConnection* connection);

  static void SendPing(QuicConnection* connection);

  static void SetLastPacketDestinationAddress(QuicConnection* connection,
                                              const QuicSocketAddress& address);

  static QuicPathValidator* path_validator(QuicConnection* connection);

  static QuicByteCount BytesReceivedOnDefaultPath(QuicConnection* connection);

  static QuicByteCount BytesSentOnAlternativePath(QuicConnection* connection);

  static QuicByteCount BytesReceivedOnAlternativePath(
      QuicConnection* connection);

  static QuicConnectionId GetClientConnectionIdOnAlternativePath(
      const QuicConnection* connection);

  static QuicConnectionId GetServerConnectionIdOnAlternativePath(
      const QuicConnection* connection);

  static bool IsAlternativePath(QuicConnection* connection,
                                const QuicSocketAddress& self_address,
                                const QuicSocketAddress& peer_address);

  static bool IsAlternativePathValidated(QuicConnection* connection);

  static QuicByteCount BytesReceivedBeforeAddressValidation(
      QuicConnection* connection);

  static void ResetPeerIssuedConnectionIdManager(QuicConnection* connection);

  static QuicConnection::PathState* GetDefaultPath(QuicConnection* connection);

  static bool IsDefaultPath(QuicConnection* connection,
                            const QuicSocketAddress& self_address,
                            const QuicSocketAddress& peer_address);

  static QuicConnection::PathState* GetAlternativePath(
      QuicConnection* connection);

  static void RetirePeerIssuedConnectionIdsNoLongerOnPath(
      QuicConnection* connection);

  static bool HasUnusedPeerIssuedConnectionId(const QuicConnection* connection);

  static bool HasSelfIssuedConnectionIdToConsume(
      const QuicConnection* connection);

  static QuicSelfIssuedConnectionIdManager* GetSelfIssuedConnectionIdManager(
      QuicConnection* connection);

  static std::unique_ptr<QuicSelfIssuedConnectionIdManager>
  MakeSelfIssuedConnectionIdManager(QuicConnection* connection);

  static void SetLastDecryptedLevel(QuicConnection* connection,
                                    EncryptionLevel level);

  static QuicCoalescedPacket& GetCoalescedPacket(QuicConnection* connection);

  static void FlushCoalescedPacket(QuicConnection* connection);

  static QuicAlarm* GetMultiPortProbingAlarm(QuicConnection* connection);

  static void SetInProbeTimeOut(QuicConnection* connection, bool value);

  static QuicSocketAddress GetReceivedServerPreferredAddress(
      QuicConnection* connection);

  static bool TestLastReceivedPacketInfoDefaults();

  // Overrides restrictions on sending ECN for test purposes.
  static void DisableEcnCodepointValidation(QuicConnection* connection);

  static void OnForwardProgressMade(QuicConnection* connection);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_CONNECTION_PEER_H_
