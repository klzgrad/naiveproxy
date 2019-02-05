// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Provides a simple interface for QUIC tests to create a variety of packets.

#ifndef NET_QUIC_QUIC_TEST_PACKET_MAKER_H_
#define NET_QUIC_QUIC_TEST_PACKET_MAKER_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "net/base/request_priority.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/test_tools/mock_clock.h"
#include "net/third_party/quic/test_tools/mock_random.h"
#include "net/third_party/spdy/core/spdy_framer.h"
#include "net/third_party/spdy/core/spdy_protocol.h"

namespace net {
namespace test {

class QuicTestPacketMaker {
 public:
  struct Http2StreamDependency {
    quic::QuicStreamId stream_id;
    quic::QuicStreamId parent_stream_id;
    spdy::SpdyPriority spdy_priority;
  };

  // |client_headers_include_h2_stream_dependency| affects the output of
  // the MakeRequestHeaders...() methods. If its value is true, then request
  // headers are constructed with the exclusive flag set to true and the parent
  // stream id set to the |parent_stream_id| param of MakeRequestHeaders...().
  // Otherwise, headers are constructed with the exclusive flag set to false and
  // the parent stream ID set to 0 (ignoring the |parent_stream_id| param).
  QuicTestPacketMaker(quic::QuicTransportVersion version,
                      quic::QuicConnectionId connection_id,
                      quic::MockClock* clock,
                      const std::string& host,
                      quic::Perspective perspective,
                      bool client_headers_include_h2_stream_dependency);
  ~QuicTestPacketMaker();

  void set_hostname(const std::string& host);
  std::unique_ptr<quic::QuicReceivedPacket> MakeConnectivityProbingPacket(
      quic::QuicPacketNumber num,
      bool include_version);
  std::unique_ptr<quic::QuicReceivedPacket> MakePingPacket(
      quic::QuicPacketNumber num,
      bool include_version);
  std::unique_ptr<quic::QuicReceivedPacket> MakeDummyCHLOPacket(
      quic::QuicPacketNumber packet_num);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndPingPacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked);

  std::unique_ptr<quic::QuicReceivedPacket> MakeStreamIdBlockedPacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicStreamId stream_id);

  std::unique_ptr<quic::QuicReceivedPacket> MakeMaxStreamIdPacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicStreamId stream_id);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRstPacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRstPacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code,
      size_t bytes_written);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRstAndRequestHeadersPacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicStreamId rst_stream_id,
      quic::QuicRstStreamErrorCode rst_error_code,
      quic::QuicStreamId stream_id,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id,
      size_t* spdy_headers_frame_length,
      quic::QuicStreamOffset* offset);

  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndRstPacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      bool send_feedback);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndRstPacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      bool send_feedback,
      size_t bytes_written);
  std::unique_ptr<quic::QuicReceivedPacket> MakeRstAckAndConnectionClosePacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code,
      quic::QuicTime::Delta delta_time_largest_observed,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      quic::QuicErrorCode quic_error,
      const std::string& quic_error_details);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndConnectionClosePacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicTime::Delta delta_time_largest_observed,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      quic::QuicErrorCode quic_error,
      const std::string& quic_error_details);
  std::unique_ptr<quic::QuicReceivedPacket> MakeConnectionClosePacket(
      quic::QuicPacketNumber num,
      bool include_version,
      quic::QuicErrorCode quic_error,
      const std::string& quic_error_details);
  std::unique_ptr<quic::QuicReceivedPacket> MakeGoAwayPacket(
      quic::QuicPacketNumber num,
      quic::QuicErrorCode error_code,
      std::string reason_phrase);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      bool send_feedback);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicPacketNumber first_received,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      bool send_feedback);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      bool send_feedback,
      quic::QuicTime::Delta ack_delay_time);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicPacketNumber first_received,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      bool send_feedback,
      quic::QuicTime::Delta ack_delay_time);
  std::unique_ptr<quic::QuicReceivedPacket> MakeDataPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      quic::QuicStreamOffset offset,
      quic::QuicStringPiece data);
  std::unique_ptr<quic::QuicReceivedPacket> MakeForceHolDataPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      quic::QuicStreamOffset* offset,
      quic::QuicStringPiece data);
  std::unique_ptr<quic::QuicReceivedPacket> MakeMultipleDataFramesPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      quic::QuicStreamOffset offset,
      const std::vector<std::string>& data_writes);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndDataPacket(
      quic::QuicPacketNumber packet_number,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      bool fin,
      quic::QuicStreamOffset offset,
      quic::QuicStringPiece data);

  std::unique_ptr<quic::QuicReceivedPacket>
  MakeRequestHeadersAndMultipleDataFramesPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id,
      quic::QuicStreamOffset* header_stream_offset,
      size_t* spdy_headers_frame_length,
      const std::vector<std::string>& data_writes);

  // If |spdy_headers_frame_length| is non-null, it will be set to the size of
  // the SPDY headers frame created for this packet.
  std::unique_ptr<quic::QuicReceivedPacket> MakeRequestHeadersPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id,
      size_t* spdy_headers_frame_length);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRequestHeadersPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id,
      size_t* spdy_headers_frame_length,
      quic::QuicStreamOffset* offset);

  // Saves the serialized QUIC stream data in |stream_data|.
  std::unique_ptr<quic::QuicReceivedPacket> MakeRequestHeadersPacketAndSaveData(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id,
      size_t* spdy_headers_frame_length,
      quic::QuicStreamOffset* offset,
      std::string* stream_data);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRequestHeadersAndRstPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id,
      size_t* spdy_headers_frame_length,
      quic::QuicStreamOffset* header_stream_offset,
      quic::QuicRstStreamErrorCode error_code,
      size_t bytes_written);

  // Convenience method for calling MakeRequestHeadersPacket with nullptr for
  // |spdy_headers_frame_length|.
  std::unique_ptr<quic::QuicReceivedPacket>
  MakeRequestHeadersPacketWithOffsetTracking(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id,
      quic::QuicStreamOffset* offset);

  // If |spdy_headers_frame_length| is non-null, it will be set to the size of
  // the SPDY headers frame created for this packet.
  std::unique_ptr<quic::QuicReceivedPacket> MakePushPromisePacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      quic::QuicStreamId promised_stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyHeaderBlock headers,
      size_t* spdy_headers_frame_length,
      quic::QuicStreamOffset* offset);

  // If |spdy_headers_frame_length| is non-null, it will be set to the size of
  // the SPDY headers frame created for this packet.
  std::unique_ptr<quic::QuicReceivedPacket> MakeResponseHeadersPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyHeaderBlock headers,
      size_t* spdy_headers_frame_length,
      quic::QuicStreamOffset* offset);

  std::unique_ptr<quic::QuicReceivedPacket> MakeResponseHeadersPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyHeaderBlock headers,
      size_t* spdy_headers_frame_length);

  // Convenience method for calling MakeResponseHeadersPacket with nullptr for
  // |spdy_headers_frame_length|.
  std::unique_ptr<quic::QuicReceivedPacket>
  MakeResponseHeadersPacketWithOffsetTracking(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamOffset* offset);

  // Creates a packet containing the initial SETTINGS frame, and saves the
  // headers stream offset into |offset|.
  std::unique_ptr<quic::QuicReceivedPacket> MakeInitialSettingsPacket(
      quic::QuicPacketNumber packet_number,
      quic::QuicStreamOffset* offset);

  // Same as above, but also saves the serialized QUIC stream data in
  // |stream_data|.
  std::unique_ptr<quic::QuicReceivedPacket>
  MakeInitialSettingsPacketAndSaveData(quic::QuicPacketNumber packet_number,
                                       quic::QuicStreamOffset* offset,
                                       std::string* stream_data);

  std::unique_ptr<quic::QuicReceivedPacket> MakePriorityPacket(
      quic::QuicPacketNumber packet_number,
      bool should_include_version,
      quic::QuicStreamId id,
      quic::QuicStreamId parent_stream_id,
      spdy::SpdyPriority priority,
      quic::QuicStreamOffset* offset);

  std::unique_ptr<quic::QuicReceivedPacket>
  MakeAckAndMultiplePriorityFramesPacket(
      quic::QuicPacketNumber packet_number,
      bool should_include_version,
      quic::QuicPacketNumber largest_received,
      quic::QuicPacketNumber smallest_received,
      quic::QuicPacketNumber least_unacked,
      const std::vector<Http2StreamDependency>& priority_frames,
      quic::QuicStreamOffset* offset);

  void SetEncryptionLevel(quic::EncryptionLevel level);

  void SetLongHeaderType(quic::QuicLongHeaderType type);

  spdy::SpdyHeaderBlock GetRequestHeaders(const std::string& method,
                                          const std::string& scheme,
                                          const std::string& path);

  spdy::SpdyHeaderBlock ConnectRequestHeaders(const std::string& host_port);

  spdy::SpdyHeaderBlock GetResponseHeaders(const std::string& status);

  spdy::SpdyHeaderBlock GetResponseHeaders(const std::string& status,
                                           const std::string& alt_svc);

  spdy::SpdyFramer* spdy_request_framer() { return &spdy_request_framer_; }
  spdy::SpdyFramer* spdy_response_framer() { return &spdy_response_framer_; }

 private:
  std::unique_ptr<quic::QuicReceivedPacket> MakePacket(
      const quic::QuicPacketHeader& header,
      const quic::QuicFrame& frame);
  std::unique_ptr<quic::QuicReceivedPacket> MakeMultipleFramesPacket(
      const quic::QuicPacketHeader& header,
      const quic::QuicFrames& frames);

  void InitializeHeader(quic::QuicPacketNumber packet_number,
                        bool should_include_version);

  spdy::SpdySerializedFrame MakeSpdyHeadersFrame(
      quic::QuicStreamId stream_id,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id);

  bool ShouldIncludeVersion(bool include_version) const;

  quic::QuicPacketNumberLength GetPacketNumberLength() const;

  quic::QuicConnectionIdLength GetDestinationConnectionIdLength() const;

  quic::QuicConnectionIdLength GetSourceConnectionIdLength() const;

  quic::QuicTransportVersion version_;
  quic::QuicConnectionId connection_id_;
  quic::MockClock* clock_;  // Owned by QuicStreamFactory.
  std::string host_;
  spdy::SpdyFramer spdy_request_framer_;
  spdy::SpdyFramer spdy_response_framer_;
  quic::test::MockRandom random_generator_;
  quic::QuicPacketHeader header_;
  quic::Perspective perspective_;
  quic::EncryptionLevel encryption_level_;
  quic::QuicLongHeaderType long_header_type_;

  // If true, generated request headers will include non-default HTTP2 stream
  // dependency info.
  bool client_headers_include_h2_stream_dependency_;

  DISALLOW_COPY_AND_ASSIGN(QuicTestPacketMaker);
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_QUIC_TEST_PACKET_MAKER_H_
