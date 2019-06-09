// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_test_packet_maker.h"

#include <list>
#include <utility>

#include "net/quic/mock_crypto_client_stream.h"
#include "net/quic/quic_http_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_random.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_data_producer.h"

namespace net {
namespace test {
namespace {

quic::QuicAckFrame MakeAckFrame(uint64_t largest_observed) {
  quic::QuicAckFrame ack;
  ack.largest_acked = quic::QuicPacketNumber(largest_observed);
  return ack;
}

}  // namespace

QuicTestPacketMaker::QuicTestPacketMaker(
    quic::ParsedQuicVersion version,
    quic::QuicConnectionId connection_id,
    quic::MockClock* clock,
    const std::string& host,
    quic::Perspective perspective,
    bool client_headers_include_h2_stream_dependency)
    : version_(version),
      connection_id_(connection_id),
      clock_(clock),
      host_(host),
      spdy_request_framer_(spdy::SpdyFramer::ENABLE_COMPRESSION),
      spdy_response_framer_(spdy::SpdyFramer::ENABLE_COMPRESSION),
      perspective_(perspective),
      encryption_level_(quic::ENCRYPTION_FORWARD_SECURE),
      long_header_type_(quic::INVALID_PACKET_TYPE),
      client_headers_include_h2_stream_dependency_(
          client_headers_include_h2_stream_dependency &&
          version.transport_version >= quic::QUIC_VERSION_43) {
  DCHECK(!(perspective_ == quic::Perspective::IS_SERVER &&
           client_headers_include_h2_stream_dependency_));
}

QuicTestPacketMaker::~QuicTestPacketMaker() {}

void QuicTestPacketMaker::set_hostname(const std::string& host) {
  host_.assign(host);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeConnectivityProbingPacket(uint64_t num,
                                                   bool include_version) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicFramer framer(quic::test::SupportedVersions(version_),
                          clock_->Now(), perspective_,
                          quic::kQuicDefaultConnectionIdLength);
  size_t max_plaintext_size =
      framer.GetMaxPlaintextSize(quic::kDefaultMaxPacketSize);
  char buffer[quic::kDefaultMaxPacketSize];
  size_t length;
  if (version_.transport_version != quic::QUIC_VERSION_99) {
    length = framer.BuildConnectivityProbingPacket(
        header, buffer, max_plaintext_size, encryption_level_);
  } else if (perspective_ == quic::Perspective::IS_CLIENT) {
    quic::test::MockRandom rand(0);
    quic::QuicPathFrameBuffer payload;
    length = framer.BuildPaddedPathChallengePacket(
        header, buffer, max_plaintext_size, &payload, &rand, encryption_level_);
  } else {
    quic::test::MockRandom rand(0);
    quic::QuicPathFrameBuffer payload;
    rand.RandBytes(payload.data(), payload.size());
    quic::QuicDeque<quic::QuicPathFrameBuffer> payloads{payload};
    length = framer.BuildPathResponsePacket(header, buffer, max_plaintext_size,
                                            payloads, true, encryption_level_);
  }
  size_t encrypted_size = framer.EncryptInPlace(
      quic::ENCRYPTION_INITIAL, header.packet_number,
      GetStartOfEncryptedData(framer.transport_version(), header), length,
      quic::kDefaultMaxPacketSize, buffer);
  EXPECT_EQ(quic::kDefaultMaxPacketSize, encrypted_size);
  quic::QuicReceivedPacket encrypted(buffer, encrypted_size, clock_->Now(),
                                     false);
  return encrypted.Clone();
}

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakePingPacket(
    uint64_t num,
    bool include_version) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicPingFrame ping;
  return MakePacket(header, quic::QuicFrame(ping));
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeDummyCHLOPacket(uint64_t packet_num) {
  SetEncryptionLevel(quic::ENCRYPTION_INITIAL);
  InitializeHeader(packet_num, /*include_version=*/true);

  quic::CryptoHandshakeMessage message =
      MockCryptoClientStream::GetDummyCHLOMessage();
  const quic::QuicData& data = message.GetSerialized();

  quic::QuicFrames frames;
  quic::QuicCryptoFrame crypto_frame;
  quic::test::SimpleDataProducer producer;
  quic::QuicStreamFrameDataProducer* producer_p = nullptr;
  if (!QuicVersionUsesCryptoFrames(version_.transport_version)) {
    quic::QuicStreamFrame frame(
        quic::QuicUtils::GetCryptoStreamId(version_.transport_version),
        /*fin=*/false, /*offset=*/0, data.AsStringPiece());
    frames.push_back(quic::QuicFrame(frame));
  } else {
    crypto_frame =
        quic::QuicCryptoFrame(quic::ENCRYPTION_INITIAL, 0, data.length());
    producer.SaveCryptoData(quic::ENCRYPTION_INITIAL, 0, data.AsStringPiece());
    frames.push_back(quic::QuicFrame(&crypto_frame));
    producer_p = &producer;
  }
  DVLOG(1) << "Adding frame: " << frames.back();
  quic::QuicPaddingFrame padding;
  frames.push_back(quic::QuicFrame(padding));
  DVLOG(1) << "Adding frame: " << frames.back();

  std::unique_ptr<quic::QuicReceivedPacket> packet =
      MakeMultipleFramesPacket(header_, frames, producer_p);
  return packet;
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndPingPacket(uint64_t num,
                                          bool include_version,
                                          uint64_t largest_received,
                                          uint64_t smallest_received,
                                          uint64_t least_unacked) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = quic::QuicTime::Delta::Zero();
  for (uint64_t i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(
        std::make_pair(quic::QuicPacketNumber(i), clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(quic::QuicPacketNumber(1),
                         quic::QuicPacketNumber(largest_received + 1));
  }
  quic::QuicFrames frames;
  frames.push_back(quic::QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames.back();

  frames.push_back(quic::QuicFrame(quic::QuicPingFrame()));
  DVLOG(1) << "Adding frame: " << frames.back();

  return MakeMultipleFramesPacket(header, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakeRstPacket(
    uint64_t num,
    bool include_version,
    quic::QuicStreamId stream_id,
    quic::QuicRstStreamErrorCode error_code) {
  return MakeRstPacket(num, include_version, stream_id, error_code, 0,
                       /*include_stop_sending_if_v99=*/true);
}

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakeRstPacket(
    uint64_t num,
    bool include_version,
    quic::QuicStreamId stream_id,
    quic::QuicRstStreamErrorCode error_code,
    size_t bytes_written,
    bool include_stop_sending_if_v99) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicFrames frames;

  quic::QuicRstStreamFrame rst(1, stream_id, error_code, bytes_written);
  frames.push_back(quic::QuicFrame(&rst));
  DVLOG(1) << "Adding frame: " << frames.back();

  // The STOP_SENDING frame must be outside of the if (version==99) so that it
  // stays in scope until the packet is built.
  quic::QuicStopSendingFrame stop(1, stream_id, error_code);
  if (include_stop_sending_if_v99 &&
      version_.transport_version == quic::QUIC_VERSION_99) {
    frames.push_back(quic::QuicFrame(&stop));
    DVLOG(1) << "Adding frame: " << frames.back();
  }
  return MakeMultipleFramesPacket(header, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeStreamsBlockedPacket(
    uint64_t num,
    bool include_version,
    quic::QuicStreamCount stream_count,
    bool unidirectional) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicStreamsBlockedFrame frame(1, stream_count, unidirectional);
  DVLOG(1) << "Adding frame: " << quic::QuicFrame(frame);
  return MakePacket(header, quic::QuicFrame(frame));
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeMaxStreamsPacket(uint64_t num,
                                          bool include_version,
                                          quic::QuicStreamCount stream_count,
                                          bool unidirectional) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicMaxStreamsFrame frame(1, stream_count, unidirectional);
  DVLOG(1) << "Adding frame: " << quic::QuicFrame(frame);
  return MakePacket(header, quic::QuicFrame(frame));
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeRstAndRequestHeadersPacket(
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
    quic::QuicStreamOffset* offset) {
  quic::QuicRstStreamFrame rst_frame(1, rst_stream_id, rst_error_code, 0);

  spdy::SpdySerializedFrame spdy_frame = MakeSpdyHeadersFrame(
      stream_id, fin, priority, std::move(headers), parent_stream_id);
  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  quic::QuicStreamOffset header_offset = 0;
  if (offset != nullptr) {
    header_offset = *offset;
    *offset += spdy_frame.size();
  }
  quic::QuicStreamFrame headers_frame(
      quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
      header_offset,
      quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));

  quic::QuicFrames frames;
  frames.push_back(quic::QuicFrame(&rst_frame));
  DVLOG(1) << "Adding frame: " << frames.back();

  // The STOP_SENDING frame must be outside of the if (version==99) so that it
  // stays in scope until the packet is built.
  quic::QuicStopSendingFrame stop(1, rst_stream_id, rst_error_code);
  if (version_.transport_version == quic::QUIC_VERSION_99) {
    frames.push_back(quic::QuicFrame(&stop));
    DVLOG(1) << "Adding frame: " << frames.back();
  }
  frames.push_back(quic::QuicFrame(headers_frame));
  DVLOG(1) << "Adding frame: " << frames.back();

  InitializeHeader(num, include_version);
  return MakeMultipleFramesPacket(header_, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndRstPacket(
    uint64_t num,
    bool include_version,
    quic::QuicStreamId stream_id,
    quic::QuicRstStreamErrorCode error_code,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    bool send_feedback) {
  return MakeAckAndRstPacket(num, include_version, stream_id, error_code,
                             largest_received, smallest_received, least_unacked,
                             send_feedback, 0,
                             /*include_stop_sending_if_v99=*/true);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndRstPacket(
    uint64_t num,
    bool include_version,
    quic::QuicStreamId stream_id,
    quic::QuicRstStreamErrorCode error_code,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    bool send_feedback,
    size_t bytes_written,
    bool include_stop_sending_if_v99) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = quic::QuicTime::Delta::Zero();
  for (uint64_t i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(
        std::make_pair(quic::QuicPacketNumber(i), clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(quic::QuicPacketNumber(1),
                         quic::QuicPacketNumber(largest_received + 1));
  }
  quic::QuicFrames frames;
  frames.push_back(quic::QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames.back();

  quic::QuicRstStreamFrame rst(1, stream_id, error_code, bytes_written);
  frames.push_back(quic::QuicFrame(&rst));
  DVLOG(1) << "Adding frame: " << frames.back();

  // The STOP_SENDING frame must be outside of the if (version==99) so that it
  // stays in scope until the packet is built.
  quic::QuicStopSendingFrame stop(1, stream_id, error_code);
  if (version_.transport_version == quic::QUIC_VERSION_99 &&
      include_stop_sending_if_v99) {
    frames.push_back(quic::QuicFrame(&stop));
    DVLOG(1) << "Adding frame: " << frames.back();
  }

  return MakeMultipleFramesPacket(header, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeRstAckAndConnectionClosePacket(
    uint64_t num,
    bool include_version,
    quic::QuicStreamId stream_id,
    quic::QuicRstStreamErrorCode error_code,
    quic::QuicTime::Delta ack_delay_time,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    quic::QuicErrorCode quic_error,
    const std::string& quic_error_details) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicFrames frames;
  quic::QuicRstStreamFrame rst(1, stream_id, error_code, 0);
  frames.push_back(quic::QuicFrame(&rst));
  DVLOG(1) << "Adding frame: " << frames.back();

  // The STOP_SENDING frame must be outside of the if (version==99) so that it
  // stays in scope until the packet is built.
  quic::QuicStopSendingFrame stop(
      1, stream_id, static_cast<quic::QuicApplicationErrorCode>(error_code));
  if (version_.transport_version == quic::QUIC_VERSION_99) {
    frames.push_back(quic::QuicFrame(&stop));
    DVLOG(1) << "Adding frame: " << frames.back();
  }

  quic::QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = ack_delay_time;
  for (uint64_t i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(
        std::make_pair(quic::QuicPacketNumber(i), clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(quic::QuicPacketNumber(1),
                         quic::QuicPacketNumber(largest_received + 1));
  }
  frames.push_back(quic::QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames.back();

  quic::QuicConnectionCloseFrame close;
  close.quic_error_code = quic_error;
  close.error_details = quic_error_details;
  if (version_.transport_version == quic::QUIC_VERSION_99) {
    close.close_type = quic::IETF_QUIC_TRANSPORT_CONNECTION_CLOSE;
  }

  frames.push_back(quic::QuicFrame(&close));
  DVLOG(1) << "Adding frame: " << frames.back();

  return MakeMultipleFramesPacket(header, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndConnectionClosePacket(
    uint64_t num,
    bool include_version,
    quic::QuicTime::Delta ack_delay_time,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    quic::QuicErrorCode quic_error,
    const std::string& quic_error_details) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = ack_delay_time;
  for (uint64_t i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(
        std::make_pair(quic::QuicPacketNumber(i), clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(quic::QuicPacketNumber(1),
                         quic::QuicPacketNumber(largest_received + 1));
  }
  quic::QuicFrames frames;
  frames.push_back(quic::QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames.back();

  quic::QuicConnectionCloseFrame close;
  close.quic_error_code = quic_error;
  close.error_details = quic_error_details;
  if (version_.transport_version == quic::QUIC_VERSION_99) {
    close.close_type = quic::IETF_QUIC_TRANSPORT_CONNECTION_CLOSE;
  }

  frames.push_back(quic::QuicFrame(&close));
  DVLOG(1) << "Adding frame: " << frames.back();

  return MakeMultipleFramesPacket(header, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeConnectionClosePacket(
    uint64_t num,
    bool include_version,
    quic::QuicErrorCode quic_error,
    const std::string& quic_error_details) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(include_version);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicConnectionCloseFrame close;
  close.quic_error_code = quic_error;
  close.error_details = quic_error_details;
  if (version_.transport_version == quic::QUIC_VERSION_99) {
    close.close_type = quic::IETF_QUIC_TRANSPORT_CONNECTION_CLOSE;
  }

  return MakePacket(header, quic::QuicFrame(&close));
}

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakeGoAwayPacket(
    uint64_t num,
    quic::QuicErrorCode error_code,
    std::string reason_phrase) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(false);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(num);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicGoAwayFrame goaway;
  goaway.error_code = error_code;
  goaway.last_good_stream_id = 0;
  goaway.reason_phrase = reason_phrase;
  return MakePacket(header, quic::QuicFrame(&goaway));
}

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakeAckPacket(
    uint64_t packet_number,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    bool send_feedback) {
  return MakeAckPacket(packet_number, 1, largest_received, smallest_received,
                       least_unacked, send_feedback,
                       quic::QuicTime::Delta::Zero());
}

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakeAckPacket(
    uint64_t packet_number,
    uint64_t first_received,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    bool send_feedback) {
  return MakeAckPacket(packet_number, first_received, largest_received,
                       smallest_received, least_unacked, send_feedback,
                       quic::QuicTime::Delta::Zero());
}

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakeAckPacket(
    uint64_t packet_number,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    bool send_feedback,
    quic::QuicTime::Delta ack_delay_time) {
  return MakeAckPacket(packet_number, 1, largest_received, smallest_received,
                       least_unacked, send_feedback, ack_delay_time);
}

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakeAckPacket(
    uint64_t packet_number,
    uint64_t first_received,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    bool send_feedback,
    quic::QuicTime::Delta ack_delay_time) {
  quic::QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.destination_connection_id_included = HasDestinationConnectionId();
  header.source_connection_id = connection_id_;
  header.source_connection_id_included = HasSourceConnectionId();
  header.reset_flag = false;
  header.version_flag = ShouldIncludeVersion(false);
  header.long_packet_type = long_header_type_;
  header.packet_number_length = GetPacketNumberLength();
  header.packet_number = quic::QuicPacketNumber(packet_number);

  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      header.version_flag) {
    if (long_header_type_ == quic::INITIAL) {
      header.retry_token_length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  quic::QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = ack_delay_time;
  for (uint64_t i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(
        std::make_pair(quic::QuicPacketNumber(i), clock_->Now()));
  }
  if (largest_received > 0) {
    DCHECK_GE(largest_received, first_received);
    ack.packets.AddRange(quic::QuicPacketNumber(first_received),
                         quic::QuicPacketNumber(largest_received + 1));
  }
  quic::QuicFramer framer(quic::test::SupportedVersions(version_),
                          clock_->Now(), perspective_,
                          quic::kQuicDefaultConnectionIdLength);
  quic::QuicFrames frames;
  quic::QuicFrame ack_frame(&ack);
  frames.push_back(ack_frame);
  DVLOG(1) << "Adding frame: " << frames.back();

  size_t max_plaintext_size =
      framer.GetMaxPlaintextSize(quic::kDefaultMaxPacketSize);
  size_t ack_frame_length = framer.GetSerializedFrameLength(
      ack_frame, max_plaintext_size, /*first_frame*/ true, /*last_frame*/ false,
      header.packet_number_length);
  const size_t min_plaintext_size = 7;
  if (version_.HasHeaderProtection() && ack_frame_length < min_plaintext_size) {
    size_t padding_length = min_plaintext_size - ack_frame_length;
    frames.push_back(quic::QuicFrame(quic::QuicPaddingFrame(padding_length)));
  }

  std::unique_ptr<quic::QuicPacket> packet(
      quic::test::BuildUnsizedDataPacket(&framer, header, frames));
  char buffer[quic::kMaxOutgoingPacketSize];
  size_t encrypted_size =
      framer.EncryptPayload(quic::ENCRYPTION_INITIAL, header.packet_number,
                            *packet, buffer, quic::kMaxOutgoingPacketSize);
  EXPECT_NE(0u, encrypted_size);
  quic::QuicReceivedPacket encrypted(buffer, encrypted_size, clock_->Now(),
                                     false);
  return encrypted.Clone();
}

// Returns a newly created packet to send kData on stream 1.
std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakeDataPacket(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    quic::QuicStreamOffset offset,
    quic::QuicStringPiece data) {
  InitializeHeader(packet_number, should_include_version);
  quic::QuicStreamFrame frame(stream_id, fin, offset, data);
  DVLOG(1) << "Adding frame: " << frame;
  return MakePacket(header_, quic::QuicFrame(frame));
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeMultipleDataFramesPacket(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    quic::QuicStreamOffset offset,
    const std::vector<std::string>& data_writes) {
  InitializeHeader(packet_number, should_include_version);
  quic::QuicFrames data_frames;
  for (size_t i = 0; i < data_writes.size(); ++i) {
    bool is_fin = fin && (i == data_writes.size() - 1);
    quic::QuicFrame quic_frame(quic::QuicStreamFrame(
        stream_id, is_fin, offset, quic::QuicStringPiece(data_writes[i])));
    DVLOG(1) << "Adding frame: " << quic_frame;
    data_frames.push_back(quic_frame);
    offset += data_writes[i].length();
  }
  return MakeMultipleFramesPacket(header_, data_frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndDataPacket(uint64_t packet_number,
                                          bool include_version,
                                          quic::QuicStreamId stream_id,
                                          uint64_t largest_received,
                                          uint64_t smallest_received,
                                          uint64_t least_unacked,
                                          bool fin,
                                          quic::QuicStreamOffset offset,
                                          quic::QuicStringPiece data) {
  InitializeHeader(packet_number, include_version);

  quic::QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = quic::QuicTime::Delta::Zero();
  for (uint64_t i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(
        std::make_pair(quic::QuicPacketNumber(i), clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(quic::QuicPacketNumber(1),
                         quic::QuicPacketNumber(largest_received + 1));
  }
  quic::QuicFrames frames;
  frames.push_back(quic::QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames.back();

  frames.push_back(
      quic::QuicFrame(quic::QuicStreamFrame(stream_id, fin, offset, data)));
  DVLOG(1) << "Adding frame: " << frames.back();

  return MakeMultipleFramesPacket(header_, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndMultipleDataFramesPacket(
    uint64_t packet_number,
    bool include_version,
    quic::QuicStreamId stream_id,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    bool fin,
    quic::QuicStreamOffset offset,
    const std::vector<std::string>& data_writes) {
  InitializeHeader(packet_number, include_version);

  quic::QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = quic::QuicTime::Delta::Zero();
  for (uint64_t i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(
        std::make_pair(quic::QuicPacketNumber(i), clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(quic::QuicPacketNumber(1),
                         quic::QuicPacketNumber(largest_received + 1));
  }
  quic::QuicFrames frames;
  frames.push_back(quic::QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames.back();

  for (size_t i = 0; i < data_writes.size(); ++i) {
    bool is_fin = fin && (i == data_writes.size() - 1);
    quic::QuicFrame quic_frame(quic::QuicStreamFrame(
        stream_id, is_fin, offset, quic::QuicStringPiece(data_writes[i])));
    DVLOG(1) << "Adding frame: " << quic_frame;
    frames.push_back(quic_frame);
    offset += data_writes[i].length();
  }
  return MakeMultipleFramesPacket(header_, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersAndMultipleDataFramesPacket(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    quic::QuicStreamId parent_stream_id,
    quic::QuicStreamOffset* header_stream_offset,
    size_t* spdy_headers_frame_length,
    const std::vector<std::string>& data_writes) {
  InitializeHeader(packet_number, should_include_version);
  spdy::SpdySerializedFrame spdy_frame =
      MakeSpdyHeadersFrame(stream_id, fin && data_writes.empty(), priority,
                           std::move(headers), parent_stream_id);

  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  quic::QuicFrames frames;
  quic::QuicStreamOffset header_offset =
      header_stream_offset == nullptr ? 0 : *header_stream_offset;
  quic::QuicStreamFrame frame(
      quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
      header_offset,
      quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  frames.push_back(quic::QuicFrame(frame));
  DVLOG(1) << "Adding frame: " << frames.back();
  if (header_stream_offset != nullptr) {
    *header_stream_offset += spdy_frame.size();
  }

  quic::QuicStreamOffset offset = 0;
  // quic::QuicFrame takes a raw pointer. Use a std::vector here so we keep
  // StreamFrames alive until MakeMultipleFramesPacket is done.
  std::vector<std::unique_ptr<quic::QuicStreamFrame>> stream_frames;
  for (size_t i = 0; i < data_writes.size(); ++i) {
    bool is_fin = fin && (i == data_writes.size() - 1);
    quic::QuicFrame quic_frame(quic::QuicStreamFrame(
        stream_id, is_fin, offset, quic::QuicStringPiece(data_writes[i])));
    DVLOG(1) << "Adding frame: " << quic_frame;
    frames.push_back(quic_frame);
    offset += data_writes[i].length();
  }
  return MakeMultipleFramesPacket(header_, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacket(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    quic::QuicStreamId parent_stream_id,
    size_t* spdy_headers_frame_length) {
  return MakeRequestHeadersPacket(
      packet_number, stream_id, should_include_version, fin, priority,
      std::move(headers), parent_stream_id, spdy_headers_frame_length, nullptr);
}

// If |offset| is provided, will use the value when creating the packet.
// Will also update the value after packet creation.
std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacket(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    quic::QuicStreamId parent_stream_id,
    size_t* spdy_headers_frame_length,
    quic::QuicStreamOffset* offset) {
  std::string unused_stream_data;
  return MakeRequestHeadersPacketAndSaveData(
      packet_number, stream_id, should_include_version, fin, priority,
      std::move(headers), parent_stream_id, spdy_headers_frame_length, offset,
      &unused_stream_data);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacketAndSaveData(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    quic::QuicStreamId parent_stream_id,
    size_t* spdy_headers_frame_length,
    quic::QuicStreamOffset* offset,
    std::string* stream_data) {
  InitializeHeader(packet_number, should_include_version);
  spdy::SpdySerializedFrame spdy_frame = MakeSpdyHeadersFrame(
      stream_id, fin, priority, std::move(headers), parent_stream_id);
  *stream_data = std::string(spdy_frame.data(), spdy_frame.size());

  if (spdy_headers_frame_length)
    *spdy_headers_frame_length = spdy_frame.size();

  if (offset != nullptr) {
    quic::QuicStreamFrame frame(
        quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
        *offset, quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    *offset += spdy_frame.size();
    return MakePacket(header_, quic::QuicFrame(frame));
  } else {
    quic::QuicStreamFrame frame(
        quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
        0, quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));

    return MakePacket(header_, quic::QuicFrame(frame));
  }
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersAndRstPacket(
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
    size_t bytes_written) {
  spdy::SpdySerializedFrame spdy_frame = MakeSpdyHeadersFrame(
      stream_id, fin, priority, std::move(headers), parent_stream_id);
  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  quic::QuicStreamOffset header_offset = 0;
  if (header_stream_offset != nullptr) {
    header_offset = *header_stream_offset;
    *header_stream_offset += spdy_frame.size();
  }
  quic::QuicStreamFrame headers_frame(
      quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
      header_offset,
      quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));

  quic::QuicRstStreamFrame rst_frame(1, stream_id, error_code, bytes_written);

  quic::QuicFrames frames;
  frames.push_back(quic::QuicFrame(headers_frame));
  DVLOG(1) << "Adding frame: " << frames.back();
  frames.push_back(quic::QuicFrame(&rst_frame));
  DVLOG(1) << "Adding frame: " << frames.back();

  // The STOP_SENDING frame must be outside of the if (version==99) so that it
  // stays in scope until the packet is built.
  quic::QuicStopSendingFrame stop(1, stream_id, error_code);
  if (version_.transport_version == quic::QUIC_VERSION_99) {
    frames.push_back(quic::QuicFrame(&stop));
    DVLOG(1) << "Adding frame: " << frames.back();
  }

  InitializeHeader(packet_number, should_include_version);
  return MakeMultipleFramesPacket(header_, frames, nullptr);
}

spdy::SpdySerializedFrame QuicTestPacketMaker::MakeSpdyHeadersFrame(
    quic::QuicStreamId stream_id,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    quic::QuicStreamId parent_stream_id) {
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
std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeRequestHeadersPacketWithOffsetTracking(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyPriority priority,
    spdy::SpdyHeaderBlock headers,
    quic::QuicStreamId parent_stream_id,
    quic::QuicStreamOffset* offset) {
  return MakeRequestHeadersPacket(
      packet_number, stream_id, should_include_version, fin, priority,
      std::move(headers), parent_stream_id, nullptr, offset);
}

// If |offset| is provided, will use the value when creating the packet.
// Will also update the value after packet creation.
std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakePushPromisePacket(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    quic::QuicStreamId promised_stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length,
    quic::QuicStreamOffset* offset) {
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
    quic::QuicStreamFrame frame(
        quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
        *offset, quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    *offset += spdy_frame.size();
    return MakePacket(header_, quic::QuicFrame(frame));
  } else {
    quic::QuicStreamFrame frame(
        quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
        0, quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    return MakePacket(header_, quic::QuicFrame(frame));
  }
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeForceHolDataPacket(uint64_t packet_number,
                                            quic::QuicStreamId stream_id,
                                            bool should_include_version,
                                            bool fin,
                                            quic::QuicStreamOffset* offset,
                                            quic::QuicStringPiece data) {
  spdy::SpdyDataIR spdy_data(stream_id, data);
  spdy_data.set_fin(fin);
  spdy::SpdySerializedFrame spdy_frame(
      spdy_request_framer_.SerializeFrame(spdy_data));
  InitializeHeader(packet_number, should_include_version);
  quic::QuicStreamFrame quic_frame(
      quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
      *offset, quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  *offset += spdy_frame.size();
  return MakePacket(header_, quic::QuicFrame(quic_frame));
}

// If |offset| is provided, will use the value when creating the packet.
// Will also update the value after packet creation.
std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeResponseHeadersPacket(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyHeaderBlock headers,
    size_t* spdy_headers_frame_length,
    quic::QuicStreamOffset* offset) {
  InitializeHeader(packet_number, should_include_version);
  spdy::SpdySerializedFrame spdy_frame;
  spdy::SpdyHeadersIR headers_frame(stream_id, std::move(headers));
  headers_frame.set_fin(fin);
  spdy_frame = spdy_response_framer_.SerializeFrame(headers_frame);

  if (spdy_headers_frame_length) {
    *spdy_headers_frame_length = spdy_frame.size();
  }
  if (offset != nullptr) {
    quic::QuicStreamFrame frame(
        quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
        *offset, quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    *offset += spdy_frame.size();
    return MakePacket(header_, quic::QuicFrame(frame));
  } else {
    quic::QuicStreamFrame frame(
        quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
        0, quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    return MakePacket(header_, quic::QuicFrame(frame));
  }
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeResponseHeadersPacket(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
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
std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeResponseHeadersPacketWithOffsetTracking(
    uint64_t packet_number,
    quic::QuicStreamId stream_id,
    bool should_include_version,
    bool fin,
    spdy::SpdyHeaderBlock headers,
    quic::QuicStreamOffset* offset) {
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

std::unique_ptr<quic::QuicReceivedPacket> QuicTestPacketMaker::MakePacket(
    const quic::QuicPacketHeader& header,
    const quic::QuicFrame& frame) {
  quic::QuicFrames frames;
  frames.push_back(frame);
  return MakeMultipleFramesPacket(header, frames, nullptr);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeMultipleFramesPacket(
    const quic::QuicPacketHeader& header,
    const quic::QuicFrames& frames,
    quic::QuicStreamFrameDataProducer* data_producer) {
  quic::QuicFramer framer(quic::test::SupportedVersions(version_),
                          clock_->Now(), perspective_,
                          quic::kQuicDefaultConnectionIdLength);
  if (data_producer != nullptr) {
    framer.set_data_producer(data_producer);
  }
  quic::QuicFrames frames_copy = frames;
  size_t max_plaintext_size =
      framer.GetMaxPlaintextSize(quic::kDefaultMaxPacketSize);
  if (version_.HasHeaderProtection()) {
    size_t packet_size =
        quic::GetPacketHeaderSize(version_.transport_version, header);
    size_t frames_size = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
      bool first_frame = i == 0;
      bool last_frame = i == frames.size() - 1;
      const size_t frame_size = framer.GetSerializedFrameLength(
          frames[i], max_plaintext_size - packet_size, first_frame, last_frame,
          header.packet_number_length);
      packet_size += frame_size;
      frames_size += frame_size;
    }
    // This should be done by calling QuicPacketCreator::MinPlaintextPacketSize.
    const size_t min_plaintext_size = 7;
    if (frames_size < min_plaintext_size) {
      size_t padding_length = min_plaintext_size - frames_size;
      frames_copy.push_back(
          quic::QuicFrame(quic::QuicPaddingFrame(padding_length)));
    }
  }
  std::unique_ptr<quic::QuicPacket> packet(quic::test::BuildUnsizedDataPacket(
      &framer, header, frames_copy, max_plaintext_size));
  char buffer[quic::kMaxOutgoingPacketSize];
  size_t encrypted_size =
      framer.EncryptPayload(quic::ENCRYPTION_INITIAL, header.packet_number,
                            *packet, buffer, quic::kMaxOutgoingPacketSize);
  EXPECT_NE(0u, encrypted_size);
  quic::QuicReceivedPacket encrypted(buffer, encrypted_size, clock_->Now(),
                                     false);
  return encrypted.Clone();
}

void QuicTestPacketMaker::InitializeHeader(uint64_t packet_number,
                                           bool should_include_version) {
  header_.destination_connection_id = connection_id_;
  header_.destination_connection_id_included = HasDestinationConnectionId();
  header_.source_connection_id = connection_id_;
  header_.source_connection_id_included = HasSourceConnectionId();
  header_.reset_flag = false;
  header_.version_flag = ShouldIncludeVersion(should_include_version);
  header_.long_packet_type = long_header_type_;
  header_.packet_number_length = GetPacketNumberLength();
  header_.packet_number = quic::QuicPacketNumber(packet_number);
  if (quic::QuicVersionHasLongHeaderLengths(version_.transport_version) &&
      should_include_version) {
    if (long_header_type_ == quic::INITIAL) {
      header_.retry_token_length_length =
          quic::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    }
    header_.length_length = quic::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeInitialSettingsPacket(uint64_t packet_number,
                                               quic::QuicStreamOffset* offset) {
  std::string unused_data;
  return MakeInitialSettingsPacketAndSaveData(packet_number, offset,
                                              &unused_data);
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeInitialSettingsPacketAndSaveData(
    uint64_t packet_number,
    quic::QuicStreamOffset* offset,
    std::string* stream_data) {
  spdy::SpdySettingsIR settings_frame;
  settings_frame.AddSetting(spdy::SETTINGS_MAX_HEADER_LIST_SIZE,
                            quic::kDefaultMaxUncompressedHeaderSize);
  spdy::SpdySerializedFrame spdy_frame(
      spdy_request_framer_.SerializeFrame(settings_frame));
  InitializeHeader(packet_number, /*should_include_version*/ true);
  *stream_data = std::string(spdy_frame.data(), spdy_frame.size());
  if (offset != nullptr) {
    quic::QuicStreamFrame quic_frame(
        quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
        *offset, quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
    *offset += spdy_frame.size();
    return MakePacket(header_, quic::QuicFrame(quic_frame));
  }
  quic::QuicStreamFrame quic_frame(
      quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false, 0,
      quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  return MakePacket(header_, quic::QuicFrame(quic_frame));
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakePriorityPacket(uint64_t packet_number,
                                        bool should_include_version,
                                        quic::QuicStreamId id,
                                        quic::QuicStreamId parent_stream_id,
                                        spdy::SpdyPriority priority,
                                        quic::QuicStreamOffset* offset) {
  if (!client_headers_include_h2_stream_dependency_) {
    parent_stream_id = 0;
  }
  int weight = spdy::Spdy3PriorityToHttp2Weight(priority);
  bool exclusive = client_headers_include_h2_stream_dependency_;
  spdy::SpdyPriorityIR priority_frame(id, parent_stream_id, weight, exclusive);
  spdy::SpdySerializedFrame spdy_frame(
      spdy_request_framer_.SerializeFrame(priority_frame));

  quic::QuicStreamOffset header_offset = 0;
  if (offset != nullptr) {
    header_offset = *offset;
    *offset += spdy_frame.size();
  }
  quic::QuicStreamFrame quic_frame(
      quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
      header_offset,
      quic::QuicStringPiece(spdy_frame.data(), spdy_frame.size()));
  DVLOG(1) << "Adding frame: " << quic::QuicFrame(quic_frame);
  InitializeHeader(packet_number, should_include_version);
  return MakePacket(header_, quic::QuicFrame(quic_frame));
}

std::unique_ptr<quic::QuicReceivedPacket>
QuicTestPacketMaker::MakeAckAndMultiplePriorityFramesPacket(
    uint64_t packet_number,
    bool should_include_version,
    uint64_t largest_received,
    uint64_t smallest_received,
    uint64_t least_unacked,
    const std::vector<Http2StreamDependency>& priority_frames,
    quic::QuicStreamOffset* offset) {
  quic::QuicAckFrame ack(MakeAckFrame(largest_received));
  ack.ack_delay_time = quic::QuicTime::Delta::Zero();
  for (uint64_t i = smallest_received; i <= largest_received; ++i) {
    ack.received_packet_times.push_back(
        std::make_pair(quic::QuicPacketNumber(i), clock_->Now()));
  }
  if (largest_received > 0) {
    ack.packets.AddRange(quic::QuicPacketNumber(1),
                         quic::QuicPacketNumber(largest_received + 1));
  }
  quic::QuicFrames frames;
  frames.push_back(quic::QuicFrame(&ack));
  DVLOG(1) << "Adding frame: " << frames.back();

  const bool exclusive = client_headers_include_h2_stream_dependency_;
  quic::QuicStreamOffset header_offset = 0;
  if (offset == nullptr) {
    offset = &header_offset;
  }
  // Keep SpdySerializedFrames alive until MakeMultipleFramesPacket is done.
  std::vector<std::unique_ptr<spdy::SpdySerializedFrame>> spdy_frames;
  for (const Http2StreamDependency& info : priority_frames) {
    spdy::SpdyPriorityIR priority_frame(
        info.stream_id, info.parent_stream_id,
        spdy::Spdy3PriorityToHttp2Weight(info.spdy_priority), exclusive);

    spdy_frames.push_back(std::make_unique<spdy::SpdySerializedFrame>(
        spdy_request_framer_.SerializeFrame(priority_frame)));

    spdy::SpdySerializedFrame* spdy_frame = spdy_frames.back().get();
    quic::QuicStreamFrame stream_frame(
        quic::QuicUtils::GetHeadersStreamId(version_.transport_version), false,
        *offset, quic::QuicStringPiece(spdy_frame->data(), spdy_frame->size()));
    *offset += spdy_frame->size();

    frames.push_back(quic::QuicFrame(stream_frame));
    DVLOG(1) << "Adding frame: " << frames.back();
  }

  InitializeHeader(packet_number, should_include_version);
  return MakeMultipleFramesPacket(header_, frames, nullptr);
}

void QuicTestPacketMaker::SetEncryptionLevel(quic::EncryptionLevel level) {
  encryption_level_ = level;
    switch (level) {
      case quic::ENCRYPTION_INITIAL:
        long_header_type_ = quic::INITIAL;
        break;
      case quic::ENCRYPTION_ZERO_RTT:
        long_header_type_ = quic::ZERO_RTT_PROTECTED;
        break;
      case quic::ENCRYPTION_FORWARD_SECURE:
        long_header_type_ = quic::INVALID_PACKET_TYPE;
        break;
      default:
        QUIC_BUG << quic::QuicUtils::EncryptionLevelToString(level);
        long_header_type_ = quic::INVALID_PACKET_TYPE;
    }
}

bool QuicTestPacketMaker::ShouldIncludeVersion(bool include_version) const {
  if (version_.transport_version > quic::QUIC_VERSION_43) {
    return encryption_level_ < quic::ENCRYPTION_FORWARD_SECURE;
  }
  return include_version;
}

quic::QuicPacketNumberLength QuicTestPacketMaker::GetPacketNumberLength()
    const {
  if (version_.transport_version > quic::QUIC_VERSION_43 &&
      encryption_level_ < quic::ENCRYPTION_FORWARD_SECURE &&
      version_.transport_version != quic::QUIC_VERSION_99) {
    return quic::PACKET_4BYTE_PACKET_NUMBER;
  }
  return quic::PACKET_1BYTE_PACKET_NUMBER;
}

quic::QuicConnectionIdIncluded QuicTestPacketMaker::HasDestinationConnectionId()
    const {
  if (perspective_ == quic::Perspective::IS_SERVER &&
      version_.transport_version > quic::QUIC_VERSION_43) {
    return quic::CONNECTION_ID_ABSENT;
  }
  return quic::CONNECTION_ID_PRESENT;
}

quic::QuicConnectionIdIncluded QuicTestPacketMaker::HasSourceConnectionId()
    const {
  if (perspective_ == quic::Perspective::IS_SERVER &&
      version_.transport_version > quic::QUIC_VERSION_43 &&
      encryption_level_ < quic::ENCRYPTION_FORWARD_SECURE) {
    return quic::CONNECTION_ID_PRESENT;
  }
  return quic::CONNECTION_ID_ABSENT;
}

}  // namespace test
}  // namespace net
