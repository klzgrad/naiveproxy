// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_test_packet_maker.h"

#include <list>
#include <utility>

#include "net/quic/chromium/quic_http_utils.h"
#include "net/quic/core/quic_framer.h"
#include "net/quic/core/quic_utils.h"
#include "net/quic/test_tools/quic_test_utils.h"

namespace net {
namespace test {
namespace {

QuicAckFrame MakeAckFrame(QuicPacketNumber largest_observed) {
  QuicAckFrame ack;
  ack.largest_observed = largest_observed;
  return ack;
}

}  // namespace

QuicTestPacketMaker::QuicTestPacketMaker(QuicTransportVersion version,
                                         QuicConnectionId connection_id,
                                         MockClock* clock,
                                         const std::string& host,
                                         Perspective perspective)
    : version_(version),
      connection_id_(connection_id),
      clock_(clock),
      host_(host),
      spdy_request_framer_(SpdyFramer::ENABLE_COMPRESSION),
      spdy_response_framer_(SpdyFramer::ENABLE_COMPRESSION),
      perspective_(perspective) {}

QuicTestPacketMaker::~QuicTestPacketMaker() {}

void QuicTestPacketMaker::set_hostname(const std::string& host) {
  host_.assign(host);
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakePingPacket(
    QuicPacketNumber num,
    bool include_version) {
  QuicPacketHeader header;
  header.public_header.connection_id = connection_id_;
  header.public_header.reset_flag = false;
  header.public_header.version_flag = include_version;
  header.public_header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;
  header.packet_number = num;

  QuicPingFrame ping;
  return std::unique_ptr<QuicReceivedPacket>(
      MakePacket(header, QuicFrame(ping)));
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
  header.public_header.connection_id = connection_id_;
  header.public_header.reset_flag = false;
  header.public_header.version_flag = include_version;
  header.public_header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;
  header.packet_number = num;

  QuicRstStreamFrame rst(stream_id, error_code, bytes_written);
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
  header.public_header.connection_id = connection_id_;
  header.public_header.reset_flag = false;
  header.public_header.version_flag = include_version;
  header.public_header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;
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
  stop_waiting.least_unacked = least_unacked;
  frames.push_back(QuicFrame(&stop_waiting));
  DVLOG(1) << "Adding frame: " << frames[1];

  QuicRstStreamFrame rst(stream_id, error_code, bytes_written);
  frames.push_back(QuicFrame(&rst));
  DVLOG(1) << "Adding frame: " << frames[2];

  QuicFramer framer(SupportedTransportVersions(version_), clock_->Now(),
                    perspective_);
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
  header.public_header.connection_id = connection_id_;
  header.public_header.reset_flag = false;
  header.public_header.version_flag = include_version;
  header.public_header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;
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
  stop_waiting.least_unacked = least_unacked;
  frames.push_back(QuicFrame(&stop_waiting));
  DVLOG(1) << "Adding frame: " << frames[1];

  QuicConnectionCloseFrame close;
  close.error_code = quic_error;
  close.error_details = quic_error_details;

  frames.push_back(QuicFrame(&close));
  DVLOG(1) << "Adding frame: " << frames[2];

  QuicFramer framer(SupportedTransportVersions(version_), clock_->Now(),
                    perspective_);
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
QuicTestPacketMaker::MakeConnectionClosePacket(QuicPacketNumber num) {
  QuicPacketHeader header;
  header.public_header.connection_id = connection_id_;
  header.public_header.reset_flag = false;
  header.public_header.version_flag = false;
  header.public_header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;
  header.packet_number = num;

  QuicConnectionCloseFrame close;
  close.error_code = QUIC_CRYPTO_VERSION_NOT_SUPPORTED;
  close.error_details = "Time to panic!";
  return std::unique_ptr<QuicReceivedPacket>(
      MakePacket(header, QuicFrame(&close)));
}

std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakeGoAwayPacket(
    QuicPacketNumber num,
    QuicErrorCode error_code,
    std::string reason_phrase) {
  QuicPacketHeader header;
  header.public_header.connection_id = connection_id_;
  header.public_header.reset_flag = false;
  header.public_header.version_flag = false;
  header.public_header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;
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
  header.public_header.connection_id = connection_id_;
  header.public_header.reset_flag = false;
  header.public_header.version_flag = false;
  header.public_header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;
  header.packet_number = packet_number;

  QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = ack_delay_time;
  for (QuicPacketNumber i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(std::make_pair(i, clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(1, largest_received + 1);
  }

  QuicFramer framer(SupportedTransportVersions(version_), clock_->Now(),
                    perspective_);
  QuicFrames frames;
  QuicFrame ack_frame(&ack);
  DVLOG(1) << "Adding frame: " << ack_frame;
  frames.push_back(ack_frame);

  QuicStopWaitingFrame stop_waiting;
  stop_waiting.least_unacked = least_unacked;
  frames.push_back(QuicFrame(&stop_waiting));

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
  stop_waiting.least_unacked = least_unacked;
  frames.push_back(QuicFrame(&stop_waiting));

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
    SpdyPriority priority,
    SpdyHeaderBlock headers,
    QuicStreamOffset* header_stream_offset,
    size_t* spdy_headers_frame_length,
    const std::vector<std::string>& data_writes) {
  InitializeHeader(packet_number, should_include_version);
  SpdySerializedFrame spdy_frame;
  SpdyHeadersIR headers_frame(stream_id, std::move(headers));
  headers_frame.set_fin(fin);
  headers_frame.set_weight(Spdy3PriorityToHttp2Weight(priority));
  headers_frame.set_has_priority(true);
  spdy_frame = spdy_request_framer_.SerializeFrame(headers_frame);

  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  QuicFrames frames;
  QuicStreamOffset header_offset =
      header_stream_offset == nullptr ? 0 : *header_stream_offset;
  QuicStreamFrame frame(kHeadersStreamId, false, header_offset,
                        QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  frames.push_back(QuicFrame(&frame));
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
    SpdyPriority priority,
    SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length) {
  return MakeRequestHeadersPacket(
      packet_number, stream_id, should_include_version, fin, priority,
      std::move(headers), spdy_headers_frame_length, nullptr);
}

// If |offset| is provided, will use the value when creating the packet.
// Will also update the value after packet creation.
std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacket(QuicPacketNumber packet_number,
                                              QuicStreamId stream_id,
                                              bool should_include_version,
                                              bool fin,
                                              SpdyPriority priority,
                                              SpdyHeaderBlock headers,
                                              size_t* spdy_headers_frame_length,
                                              QuicStreamOffset* offset) {
  std::string unused_stream_data;
  return MakeRequestHeadersPacketAndSaveData(
      packet_number, stream_id, should_include_version, fin, priority,
      std::move(headers), spdy_headers_frame_length, offset,
      &unused_stream_data);
}

std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacketAndSaveData(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    SpdyPriority priority,
    SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length,
    QuicStreamOffset* offset,
    std::string* stream_data) {
  InitializeHeader(packet_number, should_include_version);
  SpdySerializedFrame spdy_frame;
  SpdyHeadersIR headers_frame(stream_id, std::move(headers));
  headers_frame.set_fin(fin);
  headers_frame.set_weight(Spdy3PriorityToHttp2Weight(priority));
  headers_frame.set_has_priority(true);
  spdy_frame = spdy_request_framer_.SerializeFrame(headers_frame);
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

// Convenience method for calling MakeRequestHeadersPacket with nullptr for
// |spdy_headers_frame_length|.
std::unique_ptr<QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacketWithOffsetTracking(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    SpdyPriority priority,
    SpdyHeaderBlock headers,
    QuicStreamOffset* offset) {
  return MakeRequestHeadersPacket(packet_number, stream_id,
                                  should_include_version, fin, priority,
                                  std::move(headers), nullptr, offset);
}

// If |offset| is provided, will use the value when creating the packet.
// Will also update the value after packet creation.
std::unique_ptr<QuicReceivedPacket> QuicTestPacketMaker::MakePushPromisePacket(
    QuicPacketNumber packet_number,
    QuicStreamId stream_id,
    QuicStreamId promised_stream_id,
    bool should_include_version,
    bool fin,
    SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length,
    QuicStreamOffset* offset) {
  InitializeHeader(packet_number, should_include_version);
  SpdySerializedFrame spdy_frame;
  SpdyPushPromiseIR promise_frame(stream_id, promised_stream_id,
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
  SpdyDataIR spdy_data(stream_id, data);
  spdy_data.set_fin(fin);
  SpdySerializedFrame spdy_frame(
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
    SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length,
    QuicStreamOffset* offset) {
  InitializeHeader(packet_number, should_include_version);
  SpdySerializedFrame spdy_frame;
  SpdyHeadersIR headers_frame(stream_id, std::move(headers));
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
    SpdyHeaderBlock headers,
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
    SpdyHeaderBlock headers,
    QuicStreamOffset* offset) {
  return MakeResponseHeadersPacket(packet_number, stream_id,
                                   should_include_version, fin,
                                   std::move(headers), nullptr, offset);
}

SpdyHeaderBlock QuicTestPacketMaker::GetRequestHeaders(
    const std::string& method,
    const std::string& scheme,
    const std::string& path) {
  SpdyHeaderBlock headers;
  headers[":method"] = method;
  headers[":authority"] = host_;
  headers[":scheme"] = scheme;
  headers[":path"] = path;
  return headers;
}

SpdyHeaderBlock QuicTestPacketMaker::GetResponseHeaders(
    const std::string& status) {
  SpdyHeaderBlock headers;
  headers[":status"] = status;
  headers["content-type"] = "text/plain";
  return headers;
}

SpdyHeaderBlock QuicTestPacketMaker::GetResponseHeaders(
    const std::string& status,
    const std::string& alt_svc) {
  SpdyHeaderBlock headers;
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
  QuicFramer framer(SupportedTransportVersions(version_), clock_->Now(),
                    perspective_);
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
  header_.public_header.connection_id = connection_id_;
  header_.public_header.reset_flag = false;
  header_.public_header.version_flag = should_include_version;
  header_.public_header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;
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
  SpdySettingsIR settings_frame;
  settings_frame.AddSetting(SETTINGS_MAX_HEADER_LIST_SIZE,
                            kDefaultMaxUncompressedHeaderSize);
  SpdySerializedFrame spdy_frame(
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

}  // namespace test
}  // namespace net
