// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/http_encoder.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

namespace {

bool WriteFrameHeader(QuicByteCount length, HttpFrameType type,
                      QuicDataWriter* writer) {
  return writer->WriteVarInt62(static_cast<uint64_t>(type)) &&
         writer->WriteVarInt62(length);
}

QuicByteCount GetTotalLength(QuicByteCount payload_length, HttpFrameType type) {
  return QuicDataWriter::GetVarInt62Len(payload_length) +
         QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(type)) +
         payload_length;
}

}  // namespace

QuicByteCount HttpEncoder::GetDataFrameHeaderLength(
    QuicByteCount payload_length) {
  QUICHE_DCHECK_NE(0u, payload_length);
  return QuicDataWriter::GetVarInt62Len(payload_length) +
         QuicDataWriter::GetVarInt62Len(
             static_cast<uint64_t>(HttpFrameType::DATA));
}

quiche::QuicheBuffer HttpEncoder::SerializeDataFrameHeader(
    QuicByteCount payload_length, quiche::QuicheBufferAllocator* allocator) {
  QUICHE_DCHECK_NE(0u, payload_length);
  QuicByteCount header_length = GetDataFrameHeaderLength(payload_length);

  quiche::QuicheBuffer header(allocator, header_length);
  QuicDataWriter writer(header.size(), header.data());

  if (WriteFrameHeader(payload_length, HttpFrameType::DATA, &writer)) {
    return header;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize data frame header.";
  return quiche::QuicheBuffer();
}

std::string HttpEncoder::SerializeHeadersFrameHeader(
    QuicByteCount payload_length) {
  QUICHE_DCHECK_NE(0u, payload_length);
  QuicByteCount header_length =
      QuicDataWriter::GetVarInt62Len(payload_length) +
      QuicDataWriter::GetVarInt62Len(
          static_cast<uint64_t>(HttpFrameType::HEADERS));

  std::string frame;
  frame.resize(header_length);
  QuicDataWriter writer(header_length, frame.data());

  if (WriteFrameHeader(payload_length, HttpFrameType::HEADERS, &writer)) {
    return frame;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize headers "
         "frame header.";
  return {};
}

std::string HttpEncoder::SerializeSettingsFrame(const SettingsFrame& settings) {
  QuicByteCount payload_length = 0;
  std::vector<std::pair<uint64_t, uint64_t>> ordered_settings{
      settings.values.begin(), settings.values.end()};
  std::sort(ordered_settings.begin(), ordered_settings.end());
  // Calculate the payload length.
  for (const auto& p : ordered_settings) {
    payload_length += QuicDataWriter::GetVarInt62Len(p.first);
    payload_length += QuicDataWriter::GetVarInt62Len(p.second);
  }

  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::SETTINGS);

  std::string frame;
  frame.resize(total_length);
  QuicDataWriter writer(total_length, frame.data());

  if (!WriteFrameHeader(payload_length, HttpFrameType::SETTINGS, &writer)) {
    QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                        "settings frame header.";
    return {};
  }

  for (const auto& p : ordered_settings) {
    if (!writer.WriteVarInt62(p.first) || !writer.WriteVarInt62(p.second)) {
      QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                          "settings frame payload.";
      return {};
    }
  }

  return frame;
}

std::string HttpEncoder::SerializeGoAwayFrame(const GoAwayFrame& goaway) {
  QuicByteCount payload_length = QuicDataWriter::GetVarInt62Len(goaway.id);
  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::GOAWAY);

  std::string frame;
  frame.resize(total_length);
  QuicDataWriter writer(total_length, frame.data());

  if (WriteFrameHeader(payload_length, HttpFrameType::GOAWAY, &writer) &&
      writer.WriteVarInt62(goaway.id)) {
    return frame;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize goaway frame.";
  return {};
}

std::string HttpEncoder::SerializePriorityUpdateFrame(
    const PriorityUpdateFrame& priority_update) {
  QuicByteCount payload_length =
      QuicDataWriter::GetVarInt62Len(priority_update.prioritized_element_id) +
      priority_update.priority_field_value.size();
  QuicByteCount total_length = GetTotalLength(
      payload_length, HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM);

  std::string frame;
  frame.resize(total_length);
  QuicDataWriter writer(total_length, frame.data());

  if (WriteFrameHeader(payload_length,
                       HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM,
                       &writer) &&
      writer.WriteVarInt62(priority_update.prioritized_element_id) &&
      writer.WriteBytes(priority_update.priority_field_value.data(),
                        priority_update.priority_field_value.size())) {
    return frame;
  }

  QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                      "PRIORITY_UPDATE frame.";
  return {};
}

std::string HttpEncoder::SerializeAcceptChFrame(
    const AcceptChFrame& accept_ch) {
  QuicByteCount payload_length = 0;
  for (const auto& entry : accept_ch.entries) {
    payload_length += QuicDataWriter::GetVarInt62Len(entry.origin.size());
    payload_length += entry.origin.size();
    payload_length += QuicDataWriter::GetVarInt62Len(entry.value.size());
    payload_length += entry.value.size();
  }

  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::ACCEPT_CH);

  std::string frame;
  frame.resize(total_length);
  QuicDataWriter writer(total_length, frame.data());

  if (!WriteFrameHeader(payload_length, HttpFrameType::ACCEPT_CH, &writer)) {
    QUIC_DLOG(ERROR)
        << "Http encoder failed to serialize ACCEPT_CH frame header.";
    return {};
  }

  for (const auto& entry : accept_ch.entries) {
    if (!writer.WriteStringPieceVarInt62(entry.origin) ||
        !writer.WriteStringPieceVarInt62(entry.value)) {
      QUIC_DLOG(ERROR)
          << "Http encoder failed to serialize ACCEPT_CH frame payload.";
      return {};
    }
  }

  return frame;
}

std::string HttpEncoder::SerializeGreasingFrame() {
  uint64_t frame_type;
  QuicByteCount payload_length;
  std::string payload;
  if (!GetQuicFlag(quic_enable_http3_grease_randomness)) {
    frame_type = 0x40;
    payload_length = 1;
    payload = "a";
  } else {
    uint32_t result;
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
    frame_type = 0x1fULL * static_cast<uint64_t>(result) + 0x21ULL;

    // The payload length is random but within [0, 3];
    payload_length = result % 4;

    if (payload_length > 0) {
      payload.resize(payload_length);
      QuicRandom::GetInstance()->RandBytes(payload.data(), payload_length);
    }
  }
  QuicByteCount total_length = QuicDataWriter::GetVarInt62Len(frame_type) +
                               QuicDataWriter::GetVarInt62Len(payload_length) +
                               payload_length;

  std::string frame;
  frame.resize(total_length);
  QuicDataWriter writer(total_length, frame.data());

  bool success =
      writer.WriteVarInt62(frame_type) && writer.WriteVarInt62(payload_length);

  if (payload_length > 0) {
    success &= writer.WriteBytes(payload.data(), payload_length);
  }

  if (success) {
    return frame;
  }

  QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                      "greasing frame.";
  return {};
}

std::string HttpEncoder::SerializeWebTransportStreamFrameHeader(
    WebTransportSessionId session_id) {
  uint64_t stream_type =
      static_cast<uint64_t>(HttpFrameType::WEBTRANSPORT_STREAM);
  QuicByteCount header_length = QuicDataWriter::GetVarInt62Len(stream_type) +
                                QuicDataWriter::GetVarInt62Len(session_id);

  std::string frame;
  frame.resize(header_length);
  QuicDataWriter writer(header_length, frame.data());

  bool success =
      writer.WriteVarInt62(stream_type) && writer.WriteVarInt62(session_id);
  if (success && writer.remaining() == 0) {
    return frame;
  }

  QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                      "WEBTRANSPORT_STREAM frame header.";
  return {};
}

std::string HttpEncoder::SerializeMetadataFrameHeader(
    QuicByteCount payload_length) {
  QUICHE_DCHECK_NE(0u, payload_length);
  QuicByteCount header_length =
      QuicDataWriter::GetVarInt62Len(payload_length) +
      QuicDataWriter::GetVarInt62Len(
          static_cast<uint64_t>(HttpFrameType::METADATA));

  std::string frame;
  frame.resize(header_length);
  QuicDataWriter writer(header_length, frame.data());

  if (WriteFrameHeader(payload_length, HttpFrameType::METADATA, &writer)) {
    return frame;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize METADATA "
         "frame header.";
  return {};
}

}  // namespace quic
