// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/quic_connection_peer.h"

#include "net/quic/core/congestion_control/send_algorithm_interface.h"
#include "net/quic/core/quic_packet_writer.h"
#include "net/quic/core/quic_received_packet_manager.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/test_tools/quic_framer_peer.h"
#include "net/quic/test_tools/quic_packet_generator_peer.h"
#include "net/quic/test_tools/quic_sent_packet_manager_peer.h"

namespace net {
namespace test {

// static
void QuicConnectionPeer::SendAck(QuicConnection* connection) {
  connection->SendAck();
}

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
const QuicFrame QuicConnectionPeer::GetUpdatedAckFrame(
    QuicConnection* connection) {
  return connection->GetUpdatedAckFrame();
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
QuicAlarm* QuicConnectionPeer::GetResumeWritesAlarm(
    QuicConnection* connection) {
  return connection->resume_writes_alarm_.get();
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
                                    QuicConnection::AckMode ack_mode) {
  connection->ack_mode_ = ack_mode;
}

// static
void QuicConnectionPeer::SetAckDecimationDelay(QuicConnection* connection,
                                               float ack_decimation_delay) {
  connection->ack_decimation_delay_ = ack_decimation_delay;
}

// static
bool QuicConnectionPeer::HasRetransmittableFrames(
    QuicConnection* connection,
    QuicPacketNumber packet_number) {
  return QuicSentPacketManagerPeer::HasRetransmittableFrames(
      GetSentPacketManager(connection), packet_number);
}

// static
void QuicConnectionPeer::SetNoStopWaitingFrames(QuicConnection* connection,
                                                bool no_stop_waiting_frames) {
  connection->no_stop_waiting_frames_ = no_stop_waiting_frames;
}

}  // namespace test
}  // namespace net
