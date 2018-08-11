// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_test_packet_maker.h"

#include <list>
#include <utility>

#include "net/quic/chromium/quic_http_utils.h"
#include "net/third_party/quic/core/quic_framer.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"

namespace net {
namespace test {
namespace {

QuicAckFrame MakeAckFrame(QuicPacketNumber largest_observed) {
  QuicAckFrame ack;
  ack.largest_acked = largest_observed;
  return ack;
}

}  // namespace

QuicTestPacketMaker::QuicTestPacketMaker(
    QuicTransportVersion version,
    QuicConnectionId connection_id,
    MockClock* clock,
    const std::string& host,
    Perspective perspective,
    bool client_headers_include_h2_stream_dependency)
    : version_(version),
      connection_id_(connection_id),
      clock_(clock),
      host_(host),
      spdy_request_framer_(spdy::SpdyFramer::ENABLE_COMPRESSION),
      spdy_response_framer_(spdy::SpdyFramer::ENABLE_COMPRESSION),
      perspective_(perspective),
      encryption_level_(ENCRYPTION_FORWARD_SECURE),
      long_header_type_(HANDSHAKE),
      client_headers_include_h2_stream_dependency_(
          client_headers_include_h2_stream_dependency &&
          version > QUIC_VERSION_42) {
  DCHECK(!(perspective_ == Perspective::IS_SERVER &&
           client_headers_include_h2_stream_dependency_));
}

QuicTestPacketMaker::~QuicTestPacketMaker() {}

void QuicTestPacketMaker::set_hostname(const std::string& host) {
  host_.assign(host);
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeConnectivityProbingPacket(
    QuicPacketNumber num,
    bool include_version,
    QuicByteCount packet_length) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = num;

  QuicFramer framer(
      SupportedVersions(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version_)),
      clock_->Now(), perspective_);

  char buffer[kMaxPacketSize];
  size_t length =
      framer.BuildConnectivityProbingPacket(header, buffer, packet_length);
  size_t encrypted_size = framer.EncryptInPlace(
      ENCRYPTION_NONE, header.packet_number,
      GetStartOfEncryptedData(framer.transport_version(), header), length,
      kMaxPacketSize, buffer);
  EXPECT_NE(0u, encrypted_size);
  QuicReceivedPacket encrypted(buffer, encrypted_size, clock_->Now(), false);
  return std::unique_ptr<QuicReceivedPacket>(encrypted.Clone());
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakePingPacket(
    QuicPacketNumber num,
    bool include_version) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = num;

  QuicPingFrame ping;
  return std::unique_ptr<QuicReceivedPacket>(
      MakePacket(header, QuicFrame(ping)));
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeAckAndPingPacket(
    QuicPacketNumber num,
    bool include_version,
    QuicPacketNumber largest_received,
    QuicPacketNumber smallest_received,
    QuicPacketNumber least_unacked) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = num;

  QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = QuicTime::Delta::Zero();
  for (QuicPacketNumber i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(std::make_pair(i, clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(1, largest_received + 1);
  }
  QuicFrames frames;
  frames.push_back(QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames[0];

  QuicStopWaitingFrame stop_waiting;
  if (version_ <= QUIC_VERSION_43) {
    stop_waiting.least_unacked = least_unacked;
    frames.push_back(QuicFrame(&stop_waiting));
    DVLOG(1) << "Adding frame: " << frames[1];
  }

  frames.push_back(QuicFrame(QuicPingFrame()));
  DVLOG(1) << "Adding frame: " << frames[2];

  QuicFramer framer(
      SupportedVersions(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version_)),
      clock_->Now(), perspective_);
  std::unique_ptr<QuicPacket> packet(
      BuildUnsizedDataPacket(&framer, header, frames));
  char buffer[kMaxPacketSize];
  size_t encrypted_size = framer.EncryptPayload(
      ENCRYPTION_NONE, header.packet_number, *packet, buffer, kMaxPacketSize);
  EXPECT_NE(0u, encrypted_size);
  QuicReceivedPacket encrypted(buffer, encrypted_size, QuicTime::Zero(), false);
  return std::unique_ptr<QuicReceivedPacket>(encrypted.Clone());
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeRstPacket(
    QuicPacketNumber num,
    bool include_version,
    QuicStreamId stream_id,
    QuicRstStreamErrorCode error_code) {
  return MakeRstPacket(num, include_version, stream_id, error_code, 0);
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeRstPacket(
    QuicPacketNumber num,
    bool include_version,
    QuicStreamId stream_id,
    QuicRstStreamErrorCode error_code,
    size_t bytes_written) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = num;

  QuicRstStreamFrame rst(1, stream_id, error_code, bytes_written);
  DVLOG(1) << "Adding frame: " << QuicFrame(&rst);
  return std::unique_ptr<QuicReceivedPacket>(
      MakePacket(header, QuicFrame(&rst)));
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeAckAndRstPacket(
    QuicPacketNumber num,
    bool include_version,
    QuicStreamId stream_id,
    QuicRstStreamErrorCode error_code,
    QuicPacketNumber largest_received,
    QuicPacketNumber smallest_received,
    QuicPacketNumber least_unacked,
    bool send_feedback) {
  return MakeAckAndRstPacket(num, include_version, stream_id, error_code,
                             largest_received, smallest_received, least_unacked,
                             send_feedback, 0);
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeAckAndRstPacket(
    QuicPacketNumber num,
    bool include_version,
    QuicStreamId stream_id,
    QuicRstStreamErrorCode error_code,
    QuicPacketNumber largest_received,
    QuicPacketNumber smallest_received,
    QuicPacketNumber least_unacked,
    bool send_feedback,
    size_t bytes_written) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = num;

  QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = QuicTime::Delta::Zero();
  for (QuicPacketNumber i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(std::make_pair(i, clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(1, largest_received + 1);
  }
  QuicFrames frames;
  frames.push_back(QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames[0];

  QuicStopWaitingFrame stop_waiting;
  if (version_ <= QUIC_VERSION_43) {
    stop_waiting.least_unacked = least_unacked;
    frames.push_back(QuicFrame(&stop_waiting));
    DVLOG(1) << "Adding frame: " << frames[1];
  }

  QuicRstStreamFrame rst(1, stream_id, error_code, bytes_written);
  frames.push_back(QuicFrame(&rst));
  DVLOG(1) << "Adding frame: " << frames[2];

  QuicFramer framer(
      SupportedVersions(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version_)),
      clock_->Now(), perspective_);
  std::unique_ptr<QuicPacket> packet(
      BuildUnsizedDataPacket(&framer, header, frames));
  char buffer[kMaxPacketSize];
  size_t encrypted_size = framer.EncryptPayload(
      ENCRYPTION_NONE, header.packet_number, *packet, buffer, kMaxPacketSize);
  EXPECT_NE(0u, encrypted_size);
  QuicReceivedPacket encrypted(buffer, encrypted_size, QuicTime::Zero(), false);
  return std::unique_ptr<QuicReceivedPacket>(encrypted.Clone());
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndConnectionClosePacket(
    QuicPacketNumber num,
    bool include_version,
    QuicTime::Delta ack_delay_time,
    QuicPacketNumber largest_received,
    QuicPacketNumber smallest_received,
    QuicPacketNumber least_unacked,
    QuicErrorCode quic_error,
    const std::string& quic_error_details) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = num;

  QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = ack_delay_time;
  for (QuicPacketNumber i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(std::make_pair(i, clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(1, largest_received + 1);
  }
  QuicFrames frames;
  frames.push_back(QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames[0];

  QuicStopWaitingFrame stop_waiting;
  if (version_ <= QUIC_VERSION_43) {
    stop_waiting.least_unacked = least_unacked;
    frames.push_back(QuicFrame(&stop_waiting));
    DVLOG(1) << "Adding frame: " << frames[1];
  }

  QuicConnectionCloseFrame close;
  close.error_code = quic_error;
  close.error_details = quic_error_details;

  frames.push_back(QuicFrame(&close));
  DVLOG(1) << "Adding frame: " << frames[2];

  QuicFramer framer(
      SupportedVersions(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version_)),
      clock_->Now(), perspective_);

  std::unique_ptr<QuicPacket> packet(
      BuildUnsizedDataPacket(&framer, header, frames));
  char buffer[kMaxPacketSize];
  size_t encrypted_size = framer.EncryptPayload(
      ENCRYPTION_NONE, header.packet_number, *packet, buffer, kMaxPacketSize);
  EXPECT_NE(0u, encrypted_size);
  QuicReceivedPacket encrypted(buffer, encrypted_size, clock_->Now(), false);
  return std::unique_ptr<QuicReceivedPacket>(encrypted.Clone());
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeConnectionClosePacket(
    QuicPacketNumber num,
    bool include_version,
    QuicErrorCode quic_error,
    const std::string& quic_error_details) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = num;

  QuicConnectionCloseFrame close;
  close.error_code = quic_error;
  close.error_details = quic_error_details;
  return std::unique_ptr<QuicReceivedPacket>(
      MakePacket(header, QuicFrame(&close)));
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeGoAwayPacket(
    QuicPacketNumber num,
    QuicErrorCode error_code,
    std::string reason_phrase) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(false);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = num;

  QuicGoAwayFrame goaway;
  goaway.error_code = error_code;
  goaway.last_good_stream_id = 0;
  goaway.reason_phrase = reason_phrase;
  return std::unique_ptr<QuicReceivedPacket>(
      MakePacket(header, QuicFrame(&goaway)));
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeAckPacket(
    QuicPacketNumber packet_number,
    QuicPacketNumber largest_received,
    QuicPacketNumber smallest_received,
    QuicPacketNumber least_unacked,
    bool send_feedback) {
  return MakeAckPacket(packet_number, largest_received, smallest_received,
                       least_unacked, send_feedback, QuicTime::Delta::Zero());
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeAckPacket(
    QuicPacketNumber packet_number,
    QuicPacketNumber largest_received,
    QuicPacketNumber smallest_received,
    QuicPacketNumber least_unacked,
    bool send_feedback,
    QuicTime::Delta ack_delay_time) {
  QuicPacketHeader header;
  header.connection_id = connection_id_;
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(false);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = packet_number;

  QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = ack_delay_time;
  for (QuicPacketNumber i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(std::make_pair(i, clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(1, largest_received + 1);
  }
  QuicFramer framer(
      SupportedVersions(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version_)),
      clock_->Now(), perspective_);
  QuicFrames frames;
  QuicFrame ack_frame(&ack);
  DVLOG(1) << "Adding frame: " << ack_frame;
  frames.push_back(ack_frame);

  QuicStopWaitingFrame stop_waiting;
  if (version_ <= QUIC_VERSION_43) {
    stop_waiting.least_unacked = least_unacked;
    frames.push_back(QuicFrame(&stop_waiting));
  }

  std::unique_ptr<QuicPacket> packet(
      BuildUnsizedDataPacket(&framer, header, frames));
  char buffer[kMaxPacketSize];
  size_t encrypted_size = framer.EncryptPayload(
      ENCRYPTION_NONE, header.packet_number, *packet, buffer, kMaxPacketSize);
  EXPECT_NE(0u, encrypted_size);
  QuicReceivedPacket encrypted(buffer, encrypted_size, clock_->Now(), false);
  return std::unique_ptr<QuicReceivedPacket>(encrypted.Clone());
}

// Returns a newly created packet to send kData on stream 1.
std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeDataPacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    QuicStreamOffset offset,
    QuicStringPiece data) {
  InitializeHeader(packet_number, should_include_version);
  QuicStreamFrame frame(stream_id, fin, offset, data);
  DVLOG(1) << "Adding frame: " << frame;
  return MakePacket(header_, QuicFrame(&frame));
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeMultipleDataFramesPacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    QuicStreamOffset offset,
    const std::vector<std::string>& data_writes) {
  InitializeHeader(packet_number, should_include_version);
  QuicFrames data_frames;
  // QuicFrame takes a raw pointer. Use a std::vector here so we keep
  // StreamFrames alive until MakeMultipleFramesPacket is done.
  std::vector<std::unique_ptr<QuicStreamFrame>> stream_frames;
  for (size_t i = 0; i < data_writes.size(); ++i) {
    bool is_fin = fin && (i == data_writes.size() - 1);
    stream_frames.push_back(std::make_unique<QuicStreamFrame>(
        stream_id, is_fin, offset, QuicStringPiece(data_writes[i])));
    offset += data_writes[i].length();
  }
  for (const auto& stream_frame : stream_frames) {
    QuicFrame quic_frame(stream_frame.get());
    DVLOG(1) << "Adding frame: " << quic_frame;
    data_frames.push_back(quic_frame);
  }
  return MakeMultipleFramesPacket(header_, data_frames);
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeAckAndDataPacket(
    QuicPacketNumber packet_number,
    bool include_version,
    QuicStreamId stream_id,
    QuicPacketNumber largest_received,
    QuicPacketNumber smallest_received,
    QuicPacketNumber least_unacked,
    bool fin,
    QuicStreamOffset offset,
    QuicStringPiece data) {
  InitializeHeader(packet_number, include_version);

  QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = QuicTime::Delta::Zero();
  for (QuicPacketNumber i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(std::make_pair(i, clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(1, largest_received + 1);
  }
  QuicFrames frames;
  frames.push_back(QuicFrame(&ack));

  QuicStopWaitingFrame stop_waiting;
  if (version_ <= QUIC_VERSION_43) {
    stop_waiting.least_unacked = least_unacked;
    frames.push_back(QuicFrame(&stop_waiting));
  }

  QuicStreamFrame stream_frame(stream_id, fin, offset, data);
  frames.push_back(QuicFrame(&stream_frame));

  return MakeMultipleFramesPacket(header_, frames);
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersAndMultipleDataFramesPacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    QuicStreamId parent_stream_id,
    QuicStreamOffset* header_stream_offset,
    size_t* spdy_headers_frame_length,
    const std::vector<std::string>& data_writes) {
  InitializeHeader(packet_number, should_include_version);
  spdy::SpdySerializedFrame spdy_frame =
      MakeSpdyHeadersFrame(stream_id, fin && data_writes.empty(), priority,
                           std::move(headers), parent_stream_id);

  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  QuicFrames frames;
  QuicStreamOffset header_offset =
      header_stream_offset == nullptr ? 0 : *header_stream_offset;
  QuicStreamFrame frame(kHeadersStreamId, false, header_offset,
                        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  frames.push_back(QuicFrame(&frame));
  DVLOG(1) << "Adding frame: " << frames.back();
  if (header_stream_offset != nullptr) {
    *header_stream_offset += spdy_frame.size();
  }

  QuicStreamOffset offset = 0;
  // QuicFrame takes a raw pointer. Use a std::vector here so we keep
  // StreamFrames alive until MakeMultipleFramesPacket is done.
  std::vector<std::unique_ptr<QuicStreamFrame>> stream_frames;
  for (size_t i = 0; i < data_writes.size(); ++i) {
    bool is_fin = fin && (i == data_writes.size() - 1);
    stream_frames.push_back(std::make_unique<QuicStreamFrame>(
        stream_id, is_fin, offset, QuicStringPiece(data_writes[i])));
    offset += data_writes[i].length();
  }
  for (const auto& stream_frame : stream_frames) {
    QuicFrame quic_frame(stream_frame.get());
    DVLOG(1) << "Adding frame: " << quic_frame;
    frames.push_back(quic_frame);
  }
  return MakeMultipleFramesPacket(header_, frames);
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    QuicStreamId parent_stream_id,
    size_t* spdy_headers_frame_length) {
  return MakeRequestHeadersPacket(
      packet_number, stream_id, should_include_version, fin, priority,
      std::move(headers), parent_stream_id, spdy_headers_frame_length, nullptr);
}

// If |offset| is provided, will use the value when creating the packet.
// Will also update the value after packet creation.
std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacket(QuicPacketNumber packet_number,
                                              QuicStreamId stream_id,
                                              bool should_include_version,
                                              bool fin,
                                              spdy::SpdyPriority priority,
                                              spdy::SpdyHeaderBlock headers,
                                              QuicStreamId parent_stream_id,
                                              size_t* spdy_headers_frame_length,
                                              QuicStreamOffset* offset) {
  std::string unused_stream_data;
  return MakeRequestHeadersPacketAndSaveData(
      packet_number, stream_id, should_include_version, fin, priority,
      std::move(headers), parent_stream_id, spdy_headers_frame_length, offset,
      &unused_stream_data);
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacketAndSaveData(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    QuicStreamId parent_stream_id,
    size_t* spdy_headers_frame_length,
    QuicStreamOffset* offset,
    std::string* stream_data) {
  InitializeHeader(packet_number, should_include_version);
  spdy::SpdySerializedFrame spdy_frame = MakeSpdyHeadersFrame(
      stream_id, fin, priority, std::move(headers), parent_stream_id);
  *stream_data = std::string(spdy_frame.data(), spdy_frame.size());

  if (spdy_headers_frame_length)
    *spdy_headers_frame_length = spdy_frame.size();

  if (offset != nullptr) {
    QuicStreamFrame frame(
        kHeadersStreamId, false, *offset,
        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    *offset += spdy_frame.size();
    return MakePacket(header_, QuicFrame(&frame));
  } else {
    QuicStreamFrame frame(
        kHeadersStreamId, false, 0,
        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));

    return MakePacket(header_, QuicFrame(&frame));
  }
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersAndRstPacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    QuicStreamId parent_stream_id,
    size_t* spdy_headers_frame_length,
    QuicStreamOffset* header_stream_offset,
    QuicRstStreamErrorCode error_code,
    size_t bytes_written) {
  spdy::SpdySerializedFrame spdy_frame = MakeSpdyHeadersFrame(
      stream_id, fin, priority, std::move(headers), parent_stream_id);
  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  QuicStreamOffset header_offset = 0;
  if (header_stream_offset != nullptr) {
    header_offset = *header_stream_offset;
    *header_stream_offset += spdy_frame.size();
  }
  QuicStreamFrame headers_frame(
      kHeadersStreamId, false, header_offset,
      QuicStringPiece(spdy_frame.data(), spdy_frame.size()));

  QuicRstStreamFrame rst_frame(1, stream_id, error_code, bytes_written);

  QuicFrames frames;
  frames.push_back(QuicFrame(&headers_frame));
  DVLOG(1) << "Adding frame: " << frames.back();
  frames.push_back(QuicFrame(&rst_frame));
  DVLOG(1) << "Adding frame: " << frames.back();

  InitializeHeader(packet_number, should_include_version);
  return MakeMultipleFramesPacket(header_, frames);
}

spdy::SpdySerializedFrame QuicTestPacketMaker::MakeSpdyHeadersFrame(
    QuicStreamId stream_id,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    QuicStreamId parent_stream_id) {
  spdy::SpdyHeadersIR headers_frame(stream_id, std::move(headers));
  headers_frame.set_fin(fin);
  headers_frame.set_weight(spdy::Spdy3PriorityToHttp2Weight(priority));
  headers_frame.set_has_priority(true);

  if (client_headers_include_h2_stream_dependency_) {
    headers_frame.set_parent_stream_id(parent_stream_id);
    headers_frame.set_exclusive(true);
  } else {
    headers_frame.set_parent_stream_id(0);
    headers_frame.set_exclusive(false);
  }

  return spdy_request_framer_.SerializeFrame(headers_frame);
}

// Convenience method for calling MakeRequestHeadersPacket with nullptr for
// |spdy_headers_frame_length|.
std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacketWithOffsetTracking(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    QuicStreamId parent_stream_id,
    QuicStreamOffset* offset) {
  return MakeRequestHeadersPacket(
      packet_number, stream_id, should_include_version, fin, priority,
      std::move(headers), parent_stream_id, nullptr, offset);
}

// If |offset| is provided, will use the value when creating the packet.
// Will also update the value after packet creation.
std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakePushPromisePacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    QuicStreamId promised_stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length,
    QuicStreamOffset* offset) {
  InitializeHeader(packet_number, should_include_version);
  spdy::SpdySerializedFrame spdy_frame;
  spdy::SpdyPushPromiseIR promise_frame(stream_id, promised_stream_id,
                                        std::move(headers));
  promise_frame.set_fin(fin);
  spdy_frame = spdy_request_framer_.SerializeFrame(promise_frame);
  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  if (offset != nullptr) {
    QuicStreamFrame frame(
        kHeadersStreamId, false, *offset,
        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    *offset += spdy_frame.size();
    return MakePacket(header_, QuicFrame(&frame));
  } else {
    QuicStreamFrame frame(
        kHeadersStreamId, false, 0,
        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    return MakePacket(header_, QuicFrame(&frame));
  }
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeForceHolDataPacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    QuicStreamOffset* offset,
    QuicStringPiece data) {
  spdy::SpdyDataIR spdy_data(stream_id, data);
  spdy_data.set_fin(fin);
  spdy::SpdySerializedFrame spdy_frame(
      spdy_request_framer_.SerializeFrame(spdy_data));
  InitializeHeader(packet_number, should_include_version);
  QuicStreamFrame quic_frame(
      kHeadersStreamId, false, *offset,
      QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  *offset += spdy_frame.size();
  return MakePacket(header_, QuicFrame(&quic_frame));
}

// If |offset| is provided, will use the value when creating the packet.
// Will also update the value after packet creation.
std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeResponseHeadersPacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length,
    QuicStreamOffset* offset) {
  InitializeHeader(packet_number, should_include_version);
  spdy::SpdySerializedFrame spdy_frame;
  spdy::SpdyHeadersIR headers_frame(stream_id, std::move(headers));
  headers_frame.set_fin(fin);
  spdy_frame = spdy_response_framer_.SerializeFrame(headers_frame);

  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  if (offset != nullptr) {
    QuicStreamFrame frame(
        kHeadersStreamId, false, *offset,
        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    *offset += spdy_frame.size();
    return MakePacket(header_, QuicFrame(&frame));
  } else {
    QuicStreamFrame frame(
        kHeadersStreamId, false, 0,
        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    return MakePacket(header_, QuicFrame(&frame));
  }
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeResponseHeadersPacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length) {
  return MakeResponseHeadersPacket(
      packet_number, stream_id, should_include_version, fin, std::move(headers),
      spdy_headers_frame_length, nullptr);
}

// Convenience method for calling MakeResponseHeadersPacket with nullptr for
// |spdy_headers_frame_length|.
std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeResponseHeadersPacketWithOffsetTracking(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyHeaderBlock headers,
    QuicStreamOffset* offset) {
  return MakeResponseHeadersPacket(packet_number, stream_id,
                                   should_include_version, fin,
                                   std::move(headers), nullptr, offset);
}

spdy::SpdyHeaderBlock QuicTestPacketMaker::GetRequestHeaders(
    const std::string& method,
    const std::string& scheme,
    const std::string& path) {
  spdy::SpdyHeaderBlock headers;
  headers[":method"] = method;
  headers[":authority"] = host_;
  headers[":scheme"] = scheme;
  headers[":path"] = path;
  return headers;
}

spdy::SpdyHeaderBlock QuicTestPacketMaker::ConnectRequestHeaders(
    const std::string& host_port) {
  spdy::SpdyHeaderBlock headers;
  headers[":method"] = "CONNECT";
  headers[":authority"] = host_port;
  return headers;
}

spdy::SpdyHeaderBlock QuicTestPacketMaker::GetResponseHeaders(
    const std::string& status) {
  spdy::SpdyHeaderBlock headers;
  headers[":status"] = status;
  headers["content-type"] = "text/plain";
  return headers;
}

spdy::SpdyHeaderBlock QuicTestPacketMaker::GetResponseHeaders(
    const std::string& status,
    const std::string& alt_svc) {
  spdy::SpdyHeaderBlock headers;
  headers[":status"] = status;
  headers["alt-svc"] = alt_svc;
  headers["content-type"] = "text/plain";
  return headers;
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakePacket(
    const QuicPacketHeader& header,
    const QuicFrame& frame) {
  QuicFrames frames;
  frames.push_back(frame);
  return MakeMultipleFramesPacket(header, frames);
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeMultipleFramesPacket(const QuicPacketHeader& header,
                                              const QuicFrames& frames) {
  QuicFramer framer(
      SupportedVersions(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version_)),
      clock_->Now(), perspective_);
  std::unique_ptr<QuicPacket> packet(
      BuildUnsizedDataPacket(&framer, header, frames));
  char buffer[kMaxPacketSize];
  size_t encrypted_size = framer.EncryptPayload(
      ENCRYPTION_NONE, header.packet_number, *packet, buffer, kMaxPacketSize);
  EXPECT_NE(0u, encrypted_size);
  QuicReceivedPacket encrypted(buffer, encrypted_size, clock_->Now(), false);
  return std::unique_ptr<QuicReceivedPacket>(encrypted.Clone());
}

void QuicTestPacketMaker::InitializeHeader(QuicPacketNumber packet_number,
                                           bool should_include_version) {
  header_.connection_id = connection_id_;
  header_.reset_flag = false;
  header_.version_flag = ShouldIncludeVersion(should_include_version);
  header_.long_packet_type = long_header_type_;
  header_.packet_number_length = GetPacketNumberLength();
  header_.packet_number = packet_number;
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeInitialSettingsPacket(QuicPacketNumber packet_number,
                                               QuicStreamOffset* offset) {
  std::string unused_data;
  return MakeInitialSettingsPacketAndSaveData(packet_number, offset,
                                              &unused_data);
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeInitialSettingsPacketAndSaveData(
    QuicPacketNumber packet_number,
    QuicStreamOffset* offset,
    std::string* stream_data) {
  spdy::SpdySettingsIR settings_frame;
  settings_frame.AddSetting(spdy::SETTINGS_MAX_HEADER_LIST_SIZE,
                            kDefaultMaxUncompressedHeaderSize);
  spdy::SpdySerializedFrame spdy_frame(
      spdy_request_framer_.SerializeFrame(settings_frame));
  InitializeHeader(packet_number, /*should_include_version*/ true);
  *stream_data = std::string(spdy_frame.data(), spdy_frame.size());
  if (offset != nullptr) {
    QuicStreamFrame quic_frame(
        kHeadersStreamId, false, *offset,
        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    *offset += spdy_frame.size();
    return MakePacket(header_, QuicFrame(&quic_frame));
  }
  QuicStreamFrame quic_frame(
      kHeadersStreamId, false, 0,
      QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  return MakePacket(header_, QuicFrame(&quic_frame));
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakePriorityPacket(
    QuicPacketNumber packet_number,
    bool should_include_version,
    QuicStreamId id,
    QuicStreamId parent_stream_id,
    spdy::SpdyPriority priority,
    QuicStreamOffset* offset) {
  if (!client_headers_include_h2_stream_dependency_) {
    parent_stream_id = 0;
  }
  int weight = spdy::Spdy3PriorityToHttp2Weight(priority);
  bool exclusive = client_headers_include_h2_stream_dependency_;
  spdy::SpdyPriorityIR priority_frame(id, parent_stream_id, weight, exclusive);
  spdy::SpdySerializedFrame spdy_frame(
      spdy_request_framer_.SerializeFrame(priority_frame));

  QuicStreamOffset header_offset = 0;
  if (offset != nullptr) {
    header_offset = *offset;
    *offset += spdy_frame.size();
  }
  QuicStreamFrame quic_frame(
      kHeadersStreamId, false, header_offset,
      QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  DVLOG(1) << "Adding frame: " << QuicFrame(&quic_frame);
  InitializeHeader(packet_number, should_include_version);
  return MakePacket(header_, QuicFrame(&quic_frame));
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndMultiplePriorityFramesPacket(
    QuicPacketNumber packet_number,
    bool should_include_version,
    QuicPacketNumber largest_received,
    QuicPacketNumber smallest_received,
    QuicPacketNumber least_unacked,
    const std::vector<Http2StreamDependency>& priority_frames,
    QuicStreamOffset* offset) {
  QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = QuicTime::Delta::Zero();
  for (QuicPacketNumber i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(std::make_pair(i, clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(1, largest_received + 1);
  }
  QuicFrames frames;
  frames.push_back(QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames[0];

  QuicStopWaitingFrame stop_waiting;
  if (version_ <= QUIC_VERSION_43) {
    stop_waiting.least_unacked = least_unacked;
    frames.push_back(QuicFrame(&stop_waiting));
    DVLOG(1) << "Adding frame: " << frames[1];
  }

  const bool exclusive = client_headers_include_h2_stream_dependency_;
  QuicStreamOffset header_offset = 0;
  if (offset == nullptr) {
    offset = &header_offset;
  }
  // Keep SpdySerializedFrames alive until MakeMultipleFramesPacket is done.
  // Keep StreamFrames alive until MakeMultipleFramesPacket is done.
  std::vector<std::unique_ptr<spdy::SpdySerializedFrame>> spdy_frames;
  std::vector<std::unique_ptr<QuicStreamFrame>> stream_frames;
  for (const Http2StreamDependency& info : priority_frames) {
    spdy::SpdyPriorityIR priority_frame(
        info.stream_id, info.parent_stream_id,
        spdy::Spdy3PriorityToHttp2Weight(info.spdy_priority), exclusive);

    spdy_frames.push_back(std::make_unique<spdy::SpdySerializedFrame>(
        spdy_request_framer_.SerializeFrame(priority_frame)));

    spdy::SpdySerializedFrame* spdy_frame = spdy_frames.back().get();
    stream_frames.push_back(std::make_unique<QuicStreamFrame>(
        kHeadersStreamId, false, *offset,
        QuicStringPiece(spdy_frame->data(), spdy_frame->size())));
    *offset += spdy_frame->size();

    frames.push_back(QuicFrame(stream_frames.back().get()));
    DVLOG(1) << "Adding frame: " << frames.back();
    ;
  }

  InitializeHeader(packet_number, should_include_version);
  return MakeMultipleFramesPacket(header_, frames);
}

void QuicTestPacketMaker::SetEncryptionLevel(EncryptionLevel level) {
  encryption_level_ = level;
}

void QuicTestPacketMaker::SetLongHeaderType(QuicLongHeaderType type) {
  long_header_type_ = type;
}

bool QuicTestPacketMaker::ShouldIncludeVersion(bool include_version) const {
  if (version_ == QUIC_VERSION_99) {
    return encryption_level_ < ENCRYPTION_FORWARD_SECURE;
  }
  return include_version;
}

QuicPacketNumberLength QuicTestPacketMaker::GetPacketNumberLength() const {
  if (version_ == QUIC_VERSION_99 &&
      encryption_level_ < ENCRYPTION_FORWARD_SECURE) {
    return PACKET_4BYTE_PACKET_NUMBER;
  }
  return PACKET_1BYTE_PACKET_NUMBER;
}

}  // namespace test
}  // namespace net
