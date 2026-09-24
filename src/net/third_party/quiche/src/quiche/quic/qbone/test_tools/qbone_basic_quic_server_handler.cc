// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/test_tools/qbone_basic_quic_server_handler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_stream_sequencer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/qbone/qbone_constants.h"
#include "quiche/quic/qbone/test_tools/basic_quic_server.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_status_utils.h"

namespace quic::test {

class QboneBasicQuicServerHandler::SequencerReader {
 public:
  explicit SequencerReader(const QuicStreamSequencer* absl_nonnull sequencer
                               ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : sequencer_(sequencer), offset_(sequencer->NumBytesConsumed()) {}

  bool Read(void* buffer, int num_bytes) {
    if (sequencer_->ReadableBytes() <
        offset_ - sequencer_->NumBytesConsumed() + num_bytes) {
      return false;
    }

    QuicStreamOffset new_offset = offset_;
    QuicStreamOffset end_offset = offset_ + num_bytes;
    iovec iov;
    while (new_offset < end_offset) {
      QUICHE_CHECK(sequencer_->PeekRegion(new_offset, &iov));
      QUICHE_CHECK_LE(iov.iov_len, std::numeric_limits<int>::max());

      int bytes_to_copy = std::min<int>(end_offset - new_offset, iov.iov_len);
      memcpy(reinterpret_cast<std::byte*>(buffer) + (new_offset - offset_),
             iov.iov_base, bytes_to_copy);

      new_offset += bytes_to_copy;
    }
    QUICHE_CHECK_EQ(new_offset, end_offset);

    offset_ = new_offset;
    return true;
  }

  const QuicStreamSequencer* absl_nonnull sequencer_;
  QuicStreamOffset offset_;
};

QboneBasicQuicServerHandler::QboneBasicQuicServerHandler(
    BasicQuicServer* absl_nonnull server)
    : server_(server) {}

// Lots of unnecessary copying here, but control messages are not performance-
// sensitive.
absl::Status QboneBasicQuicServerHandler::SendControlMessage(
    QuicConnectionId server_connection_id, const QboneClientRequest& request) {
  QUICHE_CHECK(connections_.contains(server_connection_id))
      << "No control stream for connection " << server_connection_id;

  QUICHE_CHECK_LE(request.ByteSizeLong(), std::numeric_limits<uint16_t>::max());
  uint16_t serialized_size = static_cast<uint16_t>(request.ByteSizeLong());

  std::vector<std::byte> buffer(sizeof(serialized_size) +
                                request.ByteSizeLong());

  // Size-prefix the serialized message. (Not network byte order.)
  memcpy(buffer.data(), &serialized_size, sizeof(serialized_size));

  if (!request.SerializeToArray(buffer.data() + sizeof(serialized_size),
                                serialized_size)) {
    return absl::InternalError("Failed to serialize QboneClientRequest");
  }

  QUICHE_ASSIGN_OR_RETURN(
      int bytes_written,
      server_->SendStreamData(
          server_connection_id,
          QboneConstants::GetControlStreamId(QUIC_VERSION_IETF_RFC_V1),
          buffer));
  QUICHE_CHECK_EQ(bytes_written, buffer.size());

  return absl::OkStatus();
}

absl::Status QboneBasicQuicServerHandler::SendTunnelPacket(
    QuicConnectionId server_connection_id, absl::Span<const std::byte> data) {
  QUICHE_CHECK(connections_.contains(server_connection_id))
      << "No control stream for connection " << server_connection_id;

  return server_->SendDatagram(server_connection_id, data);
}

void QboneBasicQuicServerHandler::OnSessionEnd(
    QuicConnectionId server_connection_id) {
  if (connections_.erase(server_connection_id) == 1) {
    OnConnectionClosed(server_connection_id);
  }
}

// Assume only QUIC_VERSION_IETF_RFC_V1 is used and validate the appropriate
// control stream ID for that QUIC version. Support only the control stream as
// the only stream in the connection.
bool QboneBasicQuicServerHandler::OnNewStream(
    QuicConnectionId server_connection_id, QuicStreamId stream_id) {
  auto [it, inserted] = connections_.insert(server_connection_id);
  QUICHE_CHECK(inserted) << "Unexpected new stream for connection "
                         << server_connection_id;

  QUICHE_CHECK_EQ(stream_id,
                  QboneConstants::GetControlStreamId(QUIC_VERSION_IETF_RFC_V1))
      << "Server received stream with unexpected ID " << stream_id;

  OnNewConnection(server_connection_id);

  return true;
}

int QboneBasicQuicServerHandler::OnStreamDataAvailable(
    QuicConnectionId server_connection_id, QuicStreamId stream_id,
    const QuicStreamSequencer& data_sequencer) {
  QUICHE_CHECK(connections_.contains(server_connection_id));
  QUICHE_CHECK_EQ(stream_id,
                  QboneConstants::GetControlStreamId(QUIC_VERSION_IETF_RFC_V1));

  SequencerReader reader(&data_sequencer);
  int total_consumed = 0;

  while (true) {
    int consumed =
        ParseAndHandleSingleControlRequest(server_connection_id, reader);
    if (consumed == 0) {
      break;
    }
    total_consumed += consumed;
  }

  return total_consumed;
}

void QboneBasicQuicServerHandler::OnDatagramReceived(
    QuicConnectionId server_connection_id, absl::Span<const std::byte> data) {
  QUICHE_CHECK(connections_.contains(server_connection_id))
      << "Unexpected datagram for connection " << server_connection_id
      << " before creating control stream.";

  OnTunnelPacket(server_connection_id, data);
}

// Lots of unnecessary copying here (and wastefully throw it away on incomplete
// messages), but control messages are not performance-sensitive.
int QboneBasicQuicServerHandler::ParseAndHandleSingleControlRequest(
    QuicConnectionId server_connection_id, SequencerReader& reader) {
  // Expect size-prepended (uint16_t) serialized QboneServerRequest proto
  // messages. (Not network byte order.)
  uint16_t request_size;
  if (!reader.Read(&request_size, sizeof(request_size))) {
    // Wait for more data.
    return 0;
  }

  std::vector<std::byte> raw_proto(request_size);
  if (!reader.Read(raw_proto.data(), request_size)) {
    // Wait for more data.
    return 0;
  }

  QboneServerRequest request;
  if (request.ParseFromArray(raw_proto.data(), raw_proto.size())) {
    OnControlMessageReceived(server_connection_id, request);
  } else {
    // Could not parse proto
    OnControlMessageParseError();
  }

  return sizeof(request_size) + request_size;
}

}  // namespace quic::test
