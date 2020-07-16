// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_TRACE_VISITOR_H_
#define QUICHE_QUIC_CORE_QUIC_TRACE_VISITOR_H_

#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "third_party/quic_trace/lib/quic_trace.pb.h"

namespace quic {

// Records a QUIC trace protocol buffer for a QuicConnection.  It's the
// responsibility of the user of this visitor to process or store the resulting
// trace, which can be accessed via trace().
class QUIC_NO_EXPORT QuicTraceVisitor : public QuicConnectionDebugVisitor {
 public:
  explicit QuicTraceVisitor(const QuicConnection* connection);

  void OnPacketSent(const SerializedPacket& serialized_packet,
                    TransmissionType transmission_type,
                    QuicTime sent_time) override;

  void OnIncomingAck(QuicPacketNumber ack_packet_number,
                     EncryptionLevel ack_decrypted_level,
                     const QuicAckFrame& ack_frame,
                     QuicTime ack_receive_time,
                     QuicPacketNumber largest_observed,
                     bool rtt_updated,
                     QuicPacketNumber least_unacked_sent_packet) override;

  void OnPacketLoss(QuicPacketNumber lost_packet_number,
                    EncryptionLevel encryption_level,
                    TransmissionType transmission_type,
                    QuicTime detection_time) override;

  void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame,
                           const QuicTime& receive_time) override;

  void OnSuccessfulVersionNegotiation(
      const ParsedQuicVersion& version) override;

  void OnApplicationLimited() override;

  void OnAdjustNetworkParameters(QuicBandwidth bandwidth,
                                 QuicTime::Delta rtt,
                                 QuicByteCount old_cwnd,
                                 QuicByteCount new_cwnd) override;

  // Returns a mutable pointer to the trace.  The trace is owned by the
  // visitor, but can be moved using Swap() method after the connection is
  // finished.
  quic_trace::Trace* trace() { return &trace_; }

 private:
  // Converts QuicTime into a microsecond delta w.r.t. the beginning of the
  // connection.
  uint64_t ConvertTimestampToRecordedFormat(QuicTime timestamp);
  // Populates a quic_trace::Frame message from |frame|.
  void PopulateFrameInfo(const QuicFrame& frame,
                         quic_trace::Frame* frame_record);
  // Populates a quic_trace::TransportState message from the associated
  // connection.
  void PopulateTransportState(quic_trace::TransportState* state);

  quic_trace::Trace trace_;
  const QuicConnection* connection_;
  const QuicTime start_time_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_TRACE_VISITOR_H_
