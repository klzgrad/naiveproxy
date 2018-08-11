// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_VISITOR_INTERFACE_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_VISITOR_INTERFACE_H_

#include "net/third_party/quic/core/quic_connection.h"
#include "net/third_party/quic/platform/api/quic_export.h"

namespace net {

// QuartcSessionVisitor observes internals of a Quartc/QUIC session for the
// purpose of gathering metrics or debug information.
class QUIC_EXPORT_PRIVATE QuartcSessionVisitor {
 public:
  virtual ~QuartcSessionVisitor() {}

  // Informs this visitor of a |QuicConnection| for the session.
  // Called once when the visitor is attached to a QuartcSession, or when a new
  // |QuicConnection| starts.
  virtual void OnQuicConnection(QuicConnection* connection) {}

  // Called when a packet has been sent.
  virtual void OnPacketSent(const SerializedPacket& serialized_packet,
                            QuicPacketNumber original_packet_number,
                            TransmissionType transmission_type,
                            QuicTime sent_time) {}

  // Called when an ack is received.
  virtual void OnIncomingAck(const QuicAckFrame& ack_frame,
                             QuicTime ack_receive_time,
                             QuicPacketNumber largest_observed,
                             bool rtt_updated,
                             QuicPacketNumber least_unacked_sent_packet) {}

  // Called when a packet is lost.
  virtual void OnPacketLoss(QuicPacketNumber lost_packet_number,
                            TransmissionType transmission_type,
                            QuicTime detection_time) {}

  // Called when a WindowUpdateFrame is received.
  virtual void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame,
                                   const QuicTime& receive_time) {}

  // Called when version negotiation succeeds.
  virtual void OnSuccessfulVersionNegotiation(
      const ParsedQuicVersion& version) {}
};

}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_VISITOR_INTERFACE_H_
