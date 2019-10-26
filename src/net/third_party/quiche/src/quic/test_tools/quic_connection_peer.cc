// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"

#include "net/third_party/quiche/src/quic/core/congestion_control/send_algorithm_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_received_packet_manager.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_framer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_packet_generator_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_sent_packet_manager_peer.h"

namespace quic {
namespace test {

// static
void QuicConnectionPeer::SetSendAlgorithm(
    QuicConnection* connection,
    SendAlgorithmInterface* send_algorithm) {
  GetSentPacketManager(connection)->SetSendAlgorithm(send_algorithm);
}

// static
void QuicConnectionPeer::SetLossAlgorithm(
    QuicConnection* connection,
    LossDetectionInterface* loss_algorithm) {
  GetSentPacketManager(connection)->loss_algorithm_ = loss_algorithm;
}

// static
void QuicConnectionPeer::PopulateStopWaitingFrame(
    QuicConnection* connection,
    QuicStopWaitingFrame* stop_waiting) {
  connection->PopulateStopWaitingFrame(stop_waiting);
}

// static
QuicConnectionVisitorInterface* QuicConnectionPeer::GetVisitor(
    QuicConnection* connection) {
  return connection->visitor_;
}

// static
QuicPacketCreator* QuicConnectionPeer::GetPacketCreator(
    QuicConnection* connection) {
  return QuicPacketGeneratorPeer::GetPacketCreator(
      &connection->packet_generator_);
}

// static
QuicPacketGenerator* QuicConnectionPeer::GetPacketGenerator(
    QuicConnection* connection) {
  return &connection->packet_generator_;
}

// static
QuicSentPacketManager* QuicConnectionPeer::GetSentPacketManager(
    QuicConnection* connection) {
  return &connection->sent_packet_manager_;
}

// static
QuicTime::Delta QuicConnectionPeer::GetNetworkTimeout(
    QuicConnection* connection) {
  return connection->idle_network_timeout_;
}

// static
void QuicConnectionPeer::SetPerspective(QuicConnection* connection,
                                        Perspective perspective) {
  connection->perspective_ = perspective;
  QuicFramerPeer::SetPerspective(&connection->framer_, perspective);
}

// static
void QuicConnectionPeer::SetSelfAddress(QuicConnection* connection,
                                        const QuicSocketAddress& self_address) {
  connection->self_address_ = self_address;
}

// static
void QuicConnectionPeer::SetPeerAddress(QuicConnection* connection,
                                        const QuicSocketAddress& peer_address) {
  connection->peer_address_ = peer_address;
}

// static
void QuicConnectionPeer::SetDirectPeerAddress(
    QuicConnection* connection,
    const QuicSocketAddress& direct_peer_address) {
  connection->direct_peer_address_ = direct_peer_address;
}

// static
void QuicConnectionPeer::SetEffectivePeerAddress(
    QuicConnection* connection,
    const QuicSocketAddress& effective_peer_address) {
  connection->effective_peer_address_ = effective_peer_address;
}

// static
bool QuicConnectionPeer::IsSilentCloseEnabled(QuicConnection* connection) {
  return connection->idle_timeout_connection_close_behavior_ ==
         ConnectionCloseBehavior::SILENT_CLOSE;
}

// static
void QuicConnectionPeer::SwapCrypters(QuicConnection* connection,
                                      QuicFramer* framer) {
  QuicFramerPeer::SwapCrypters(framer, &connection->framer_);
}

// static
void QuicConnectionPeer::SetCurrentPacket(QuicConnection* connection,
                                          QuicStringPiece current_packet) {
  connection->current_packet_data_ = current_packet.data();
  connection->last_size_ = current_packet.size();
}

// static
QuicConnectionHelperInterface* QuicConnectionPeer::GetHelper(
    QuicConnection* connection) {
  return connection->helper_;
}

// static
QuicAlarmFactory* QuicConnectionPeer::GetAlarmFactory(
    QuicConnection* connection) {
  return connection->alarm_factory_;
}

// static
QuicFramer* QuicConnectionPeer::GetFramer(QuicConnection* connection) {
  return &connection->framer_;
}

// static
QuicAlarm* QuicConnectionPeer::GetAckAlarm(QuicConnection* connection) {
  return connection->ack_alarm_.get();
}

// static
QuicAlarm* QuicConnectionPeer::GetPingAlarm(QuicConnection* connection) {
  return connection->ping_alarm_.get();
}

// static
QuicAlarm* QuicConnectionPeer::GetRetransmissionAlarm(
    QuicConnection* connection) {
  return connection->retransmission_alarm_.get();
}

// static
QuicAlarm* QuicConnectionPeer::GetSendAlarm(QuicConnection* connection) {
  return connection->send_alarm_.get();
}

// static
QuicAlarm* QuicConnectionPeer::GetTimeoutAlarm(QuicConnection* connection) {
  return connection->timeout_alarm_.get();
}

// static
QuicAlarm* QuicConnectionPeer::GetMtuDiscoveryAlarm(
    QuicConnection* connection) {
  return connection->mtu_discovery_alarm_.get();
}

// static
QuicAlarm* QuicConnectionPeer::GetPathDegradingAlarm(
    QuicConnection* connection) {
  return connection->path_degrading_alarm_.get();
}

// static
QuicAlarm* QuicConnectionPeer::GetProcessUndecryptablePacketsAlarm(
    QuicConnection* connection) {
  return connection->process_undecryptable_packets_alarm_.get();
}

// static
QuicPacketWriter* QuicConnectionPeer::GetWriter(QuicConnection* connection) {
  return connection->writer_;
}

// static
void QuicConnectionPeer::SetWriter(QuicConnection* connection,
                                   QuicPacketWriter* writer,
                                   bool owns_writer) {
  if (connection->owns_writer_) {
    delete connection->writer_;
  }
  connection->writer_ = writer;
  connection->owns_writer_ = owns_writer;
}

// static
void QuicConnectionPeer::TearDownLocalConnectionState(
    QuicConnection* connection) {
  connection->connected_ = false;
}

// static
QuicEncryptedPacket* QuicConnectionPeer::GetConnectionClosePacket(
    QuicConnection* connection) {
  if (connection->termination_packets_ == nullptr ||
      connection->termination_packets_->empty()) {
    return nullptr;
  }
  return (*connection->termination_packets_)[0].get();
}

// static
QuicPacketHeader* QuicConnectionPeer::GetLastHeader(
    QuicConnection* connection) {
  return &connection->last_header_;
}

// static
QuicConnectionStats* QuicConnectionPeer::GetStats(QuicConnection* connection) {
  return &connection->stats_;
}

// static
QuicPacketCount QuicConnectionPeer::GetPacketsBetweenMtuProbes(
    QuicConnection* connection) {
  return connection->packets_between_mtu_probes_;
}

// static
void QuicConnectionPeer::SetPacketsBetweenMtuProbes(QuicConnection* connection,
                                                    QuicPacketCount packets) {
  connection->packets_between_mtu_probes_ = packets;
}

// static
void QuicConnectionPeer::SetNextMtuProbeAt(QuicConnection* connection,
                                           QuicPacketNumber number) {
  connection->next_mtu_probe_at_ = number;
}

// static
void QuicConnectionPeer::SetAckMode(QuicConnection* connection,
                                    AckMode ack_mode) {
  for (auto& received_packet_manager :
       connection->uber_received_packet_manager_.received_packet_managers_) {
    received_packet_manager.ack_mode_ = ack_mode;
  }
}

// static
void QuicConnectionPeer::SetFastAckAfterQuiescence(
    QuicConnection* connection,
    bool fast_ack_after_quiescence) {
  for (auto& received_packet_manager :
       connection->uber_received_packet_manager_.received_packet_managers_) {
    received_packet_manager.fast_ack_after_quiescence_ =
        fast_ack_after_quiescence;
  }
}

// static
void QuicConnectionPeer::SetAckDecimationDelay(QuicConnection* connection,
                                               float ack_decimation_delay) {
  for (auto& received_packet_manager :
       connection->uber_received_packet_manager_.received_packet_managers_) {
    received_packet_manager.ack_decimation_delay_ = ack_decimation_delay;
  }
}

// static
bool QuicConnectionPeer::HasRetransmittableFrames(QuicConnection* connection,
                                                  uint64_t packet_number) {
  return QuicSentPacketManagerPeer::HasRetransmittableFrames(
      GetSentPacketManager(connection), packet_number);
}

// static
bool QuicConnectionPeer::GetNoStopWaitingFrames(QuicConnection* connection) {
  return connection->no_stop_waiting_frames_;
}

// static
void QuicConnectionPeer::SetNoStopWaitingFrames(QuicConnection* connection,
                                                bool no_stop_waiting_frames) {
  connection->no_stop_waiting_frames_ = no_stop_waiting_frames;
}

// static
void QuicConnectionPeer::SetMaxTrackedPackets(
    QuicConnection* connection,
    QuicPacketCount max_tracked_packets) {
  connection->max_tracked_packets_ = max_tracked_packets;
}

// static
void QuicConnectionPeer::SetSessionDecidesWhatToWrite(
    QuicConnection* connection) {
  connection->sent_packet_manager_.SetSessionDecideWhatToWrite(true);
  connection->packet_generator_.SetCanSetTransmissionType(true);
}

// static
void QuicConnectionPeer::SetNegotiatedVersion(QuicConnection* connection) {
  connection->version_negotiated_ = true;
}

// static
void QuicConnectionPeer::SetMaxConsecutiveNumPacketsWithNoRetransmittableFrames(
    QuicConnection* connection,
    size_t new_value) {
  connection->max_consecutive_num_packets_with_no_retransmittable_frames_ =
      new_value;
}

// static
bool QuicConnectionPeer::SupportsReleaseTime(QuicConnection* connection) {
  return connection->supports_release_time_;
}

// static
QuicConnection::PacketContent QuicConnectionPeer::GetCurrentPacketContent(
    QuicConnection* connection) {
  return connection->current_packet_content_;
}

// static
void QuicConnectionPeer::SetLastHeaderFormat(QuicConnection* connection,
                                             PacketHeaderFormat format) {
  connection->last_header_.form = format;
}

// static
void QuicConnectionPeer::AddBytesReceived(QuicConnection* connection,
                                          size_t length) {
  if (connection->EnforceAntiAmplificationLimit()) {
    connection->bytes_received_before_address_validation_ += length;
  }
}

// static
void QuicConnectionPeer::SetAddressValidated(QuicConnection* connection) {
  connection->address_validated_ = true;
}

}  // namespace test
}  // namespace quic
