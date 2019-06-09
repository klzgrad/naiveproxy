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
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_frame_data_producer.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_random.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

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
  QuicTestPacketMaker(quic::ParsedQuicVersion version,
                      quic::QuicConnectionId connection_id,
                      quic::MockClock* clock,
                      const std::string& host,
                      quic::Perspective perspective,
                      bool client_headers_include_h2_stream_dependency);
  ~QuicTestPacketMaker();

  void set_hostname(const std::string& host);
  std::unique_ptr<quic::QuicReceivedPacket> MakeConnectivityProbingPacket(
      uint64_t num,
      bool include_version);
  std::unique_ptr<quic::QuicReceivedPacket> MakePingPacket(
      uint64_t num,
      bool include_version);
  std::unique_ptr<quic::QuicReceivedPacket> MakeDummyCHLOPacket(
      uint64_t packet_num);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndPingPacket(
      uint64_t num,
      bool include_version,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked);

  std::unique_ptr<quic::QuicReceivedPacket> MakeStreamsBlockedPacket(
      uint64_t num,
      bool include_version,
      quic::QuicStreamCount stream_count,
      bool unidirectional);

  std::unique_ptr<quic::QuicReceivedPacket> MakeMaxStreamsPacket(
      uint64_t num,
      bool include_version,
      quic::QuicStreamCount stream_count,
      bool unidirectional);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRstPacket(
      uint64_t num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRstPacket(
      uint64_t num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code,
      size_t bytes_written,
      bool include_stop_sending_if_v99);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRstAndRequestHeadersPacket(
      uint64_t num,
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
      uint64_t num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      bool send_feedback);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndRstPacket(
      uint64_t num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      bool send_feedback,
      size_t bytes_written,
      bool include_stop_sending_if_v99);
  std::unique_ptr<quic::QuicReceivedPacket> MakeRstAckAndConnectionClosePacket(
      uint64_t num,
      bool include_version,
      quic::QuicStreamId stream_id,
      quic::QuicRstStreamErrorCode error_code,
      quic::QuicTime::Delta delta_time_largest_observed,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      quic::QuicErrorCode quic_error,
      const std::string& quic_error_details);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndConnectionClosePacket(
      uint64_t num,
      bool include_version,
      quic::QuicTime::Delta delta_time_largest_observed,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      quic::QuicErrorCode quic_error,
      const std::string& quic_error_details);
  std::unique_ptr<quic::QuicReceivedPacket> MakeConnectionClosePacket(
      uint64_t num,
      bool include_version,
      quic::QuicErrorCode quic_error,
      const std::string& quic_error_details);
  std::unique_ptr<quic::QuicReceivedPacket> MakeGoAwayPacket(
      uint64_t num,
      quic::QuicErrorCode error_code,
      std::string reason_phrase);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckPacket(
      uint64_t packet_number,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      bool send_feedback);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckPacket(
      uint64_t packet_number,
      uint64_t first_received,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      bool send_feedback);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckPacket(
      uint64_t packet_number,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      bool send_feedback,
      quic::QuicTime::Delta ack_delay_time);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckPacket(
      uint64_t packet_number,
      uint64_t first_received,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      bool send_feedback,
      quic::QuicTime::Delta ack_delay_time);
  std::unique_ptr<quic::QuicReceivedPacket> MakeDataPacket(
      uint64_t packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      quic::QuicStreamOffset offset,
      quic::QuicStringPiece data);
  std::unique_ptr<quic::QuicReceivedPacket> MakeForceHolDataPacket(
      uint64_t packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      quic::QuicStreamOffset* offset,
      quic::QuicStringPiece data);
  std::unique_ptr<quic::QuicReceivedPacket> MakeMultipleDataFramesPacket(
      uint64_t packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      quic::QuicStreamOffset offset,
      const std::vector<std::string>& data_writes);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndDataPacket(
      uint64_t packet_number,
      bool include_version,
      quic::QuicStreamId stream_id,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      bool fin,
      quic::QuicStreamOffset offset,
      quic::QuicStringPiece data);
  std::unique_ptr<quic::QuicReceivedPacket> MakeAckAndMultipleDataFramesPacket(
      uint64_t packet_number,
      bool include_version,
      quic::QuicStreamId stream_id,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      bool fin,
      quic::QuicStreamOffset offset,
      const std::vector<std::string>& data);

  std::unique_ptr<quic::QuicReceivedPacket>
  MakeRequestHeadersAndMultipleDataFramesPacket(
      uint64_t packet_number,
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
      uint64_t packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id,
      size_t* spdy_headers_frame_length);

  std::unique_ptr<quic::QuicReceivedPacket> MakeRequestHeadersPacket(
      uint64_t packet_number,
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
      uint64_t packet_number,
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
      uint64_t packet_number,
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
      uint64_t packet_number,
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
      uint64_t packet_number,
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
      uint64_t packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyHeaderBlock headers,
      size_t* spdy_headers_frame_length,
      quic::QuicStreamOffset* offset);

  std::unique_ptr<quic::QuicReceivedPacket> MakeResponseHeadersPacket(
      uint64_t packet_number,
      quic::QuicStreamId stream_id,
      bool should_include_version,
      bool fin,
      spdy::SpdyHeaderBlock headers,
      size_t* spdy_headers_frame_length);

  // Convenience method for calling MakeResponseHeadersPacket with nullptr for
  // |spdy_headers_frame_length|.
  std::unique_ptr<quic::QuicReceivedPacket>
  MakeResponseHeadersPacketWithOffsetTracking(uint64_t packet_number,
                                              quic::QuicStreamId stream_id,
                                              bool should_include_version,
                                              bool fin,
                                              spdy::SpdyHeaderBlock headers,
                                              quic::QuicStreamOffset* offset);

  // Creates a packet containing the initial SETTINGS frame, and saves the
  // headers stream offset into |offset|.
  std::unique_ptr<quic::QuicReceivedPacket> MakeInitialSettingsPacket(
      uint64_t packet_number,
      quic::QuicStreamOffset* offset);

  // Same as above, but also saves the serialized QUIC stream data in
  // |stream_data|.
  std::unique_ptr<quic::QuicReceivedPacket>
  MakeInitialSettingsPacketAndSaveData(uint64_t packet_number,
                                       quic::QuicStreamOffset* offset,
                                       std::string* stream_data);

  std::unique_ptr<quic::QuicReceivedPacket> MakePriorityPacket(
      uint64_t packet_number,
      bool should_include_version,
      quic::QuicStreamId id,
      quic::QuicStreamId parent_stream_id,
      spdy::SpdyPriority priority,
      quic::QuicStreamOffset* offset);

  std::unique_ptr<quic::QuicReceivedPacket>
  MakeAckAndMultiplePriorityFramesPacket(
      uint64_t packet_number,
      bool should_include_version,
      uint64_t largest_received,
      uint64_t smallest_received,
      uint64_t least_unacked,
      const std::vector<Http2StreamDependency>& priority_frames,
      quic::QuicStreamOffset* offset);

  void SetEncryptionLevel(quic::EncryptionLevel level);

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
      const quic::QuicFrames& frames,
      quic::QuicStreamFrameDataProducer* data_producer);

  void InitializeHeader(uint64_t packet_number, bool should_include_version);

  spdy::SpdySerializedFrame MakeSpdyHeadersFrame(
      quic::QuicStreamId stream_id,
      bool fin,
      spdy::SpdyPriority priority,
      spdy::SpdyHeaderBlock headers,
      quic::QuicStreamId parent_stream_id);

  bool ShouldIncludeVersion(bool include_version) const;

  quic::QuicPacketNumberLength GetPacketNumberLength() const;

  quic::QuicConnectionIdIncluded HasDestinationConnectionId() const;
  quic::QuicConnectionIdIncluded HasSourceConnectionId() const;

  quic::ParsedQuicVersion version_;
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
