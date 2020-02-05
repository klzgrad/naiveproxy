// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"

#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"

namespace quic {

namespace {

// Set the first byte of a PRIORITY frame according to its fields.
uint8_t SetPriorityFields(uint8_t num,
                          PriorityElementType type,
                          bool prioritized) {
  switch (type) {
    case REQUEST_STREAM:
      return num;
    case PUSH_STREAM:
      if (prioritized) {
        return num | (1 << 6);
      }
      return num | (1 << 4);
    case PLACEHOLDER:
      if (prioritized) {
        return num | (1 << 7);
      }
      return num | (1 << 5);
    case ROOT_OF_TREE:
      if (prioritized) {
        num = num | (1 << 6);
        return num | (1 << 7);
      }
      num = num | (1 << 4);
      return num | (1 << 5);
    default:
      QUIC_NOTREACHED();
      return num;
  }
}

bool WriteFrameHeader(QuicByteCount length,
                      HttpFrameType type,
                      QuicDataWriter* writer) {
  return writer->WriteVarInt62(static_cast<uint64_t>(type)) &&
         writer->WriteVarInt62(length);
}

QuicByteCount GetTotalLength(QuicByteCount payload_length, HttpFrameType type) {
  return QuicDataWriter::GetVarInt62Len(payload_length) +
         QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(type)) +
         payload_length;
}

// Write prioritized element id and element dependency id if needed.
bool MaybeWriteIds(const PriorityFrame& priority, QuicDataWriter* writer) {
  if (priority.prioritized_type != ROOT_OF_TREE) {
    if (!writer->WriteVarInt62(priority.prioritized_element_id)) {
      return false;
    }
  } else {
    DCHECK_EQ(0u, priority.prioritized_element_id)
        << "Prioritized element id should be 0 when prioritized type is "
           "ROOT_OF_TREE";
  }
  if (priority.dependency_type != ROOT_OF_TREE) {
    if (!writer->WriteVarInt62(priority.element_dependency_id)) {
      return false;
    }
  } else {
    DCHECK_EQ(0u, priority.element_dependency_id)
        << "Element dependency id should be 0 when dependency type is "
           "ROOT_OF_TREE";
  }
  return true;
}

}  // namespace

// static
QuicByteCount HttpEncoder::SerializeDataFrameHeader(
    QuicByteCount payload_length,
    std::unique_ptr<char[]>* output) {
  DCHECK_NE(0u, payload_length);
  QuicByteCount header_length = QuicDataWriter::GetVarInt62Len(payload_length) +
                                QuicDataWriter::GetVarInt62Len(
                                    static_cast<uint64_t>(HttpFrameType::DATA));

  output->reset(new char[header_length]);
  QuicDataWriter writer(header_length, output->get());

  if (WriteFrameHeader(payload_length, HttpFrameType::DATA, &writer)) {
    return header_length;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize data frame header.";
  return 0;
}

// static
QuicByteCount HttpEncoder::SerializeHeadersFrameHeader(
    QuicByteCount payload_length,
    std::unique_ptr<char[]>* output) {
  DCHECK_NE(0u, payload_length);
  QuicByteCount header_length =
      QuicDataWriter::GetVarInt62Len(payload_length) +
      QuicDataWriter::GetVarInt62Len(
          static_cast<uint64_t>(HttpFrameType::HEADERS));

  output->reset(new char[header_length]);
  QuicDataWriter writer(header_length, output->get());

  if (WriteFrameHeader(payload_length, HttpFrameType::HEADERS, &writer)) {
    return header_length;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize headers "
         "frame header.";
  return 0;
}

// static
QuicByteCount HttpEncoder::SerializePriorityFrame(
    const PriorityFrame& priority,
    std::unique_ptr<char[]>* output) {
  QuicByteCount payload_length =
      kPriorityFirstByteLength +
      (priority.prioritized_type == ROOT_OF_TREE
           ? 0
           : QuicDataWriter::GetVarInt62Len(priority.prioritized_element_id)) +
      (priority.dependency_type == ROOT_OF_TREE
           ? 0
           : QuicDataWriter::GetVarInt62Len(priority.element_dependency_id)) +
      kPriorityWeightLength;
  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::PRIORITY);

  output->reset(new char[total_length]);
  QuicDataWriter writer(total_length, output->get());

  if (!WriteFrameHeader(payload_length, HttpFrameType::PRIORITY, &writer)) {
    QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                        "priority frame header.";
    return 0;
  }

  // Set the first byte of the payload.
  uint8_t firstByte = 0;
  firstByte = SetPriorityFields(firstByte, priority.prioritized_type, true);
  firstByte = SetPriorityFields(firstByte, priority.dependency_type, false);
  if (priority.exclusive) {
    firstByte |= kPriorityExclusiveBit;
  }

  if (writer.WriteUInt8(firstByte) && MaybeWriteIds(priority, &writer) &&
      writer.WriteUInt8(priority.weight)) {
    return total_length;
  }
  QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                      "priority frame payload.";
  return 0;
}

// static
QuicByteCount HttpEncoder::SerializeCancelPushFrame(
    const CancelPushFrame& cancel_push,
    std::unique_ptr<char[]>* output) {
  QuicByteCount payload_length =
      QuicDataWriter::GetVarInt62Len(cancel_push.push_id);
  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::CANCEL_PUSH);

  output->reset(new char[total_length]);
  QuicDataWriter writer(total_length, output->get());

  if (WriteFrameHeader(payload_length, HttpFrameType::CANCEL_PUSH, &writer) &&
      writer.WriteVarInt62(cancel_push.push_id)) {
    return total_length;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize cancel push frame.";
  return 0;
}

// static
QuicByteCount HttpEncoder::SerializeSettingsFrame(
    const SettingsFrame& settings,
    std::unique_ptr<char[]>* output) {
  QuicByteCount payload_length = 0;
  // Calculate the payload length.
  for (auto it = settings.values.begin(); it != settings.values.end(); ++it) {
    payload_length += QuicDataWriter::GetVarInt62Len(it->first);
    payload_length += QuicDataWriter::GetVarInt62Len(it->second);
  }

  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::SETTINGS);

  output->reset(new char[total_length]);
  QuicDataWriter writer(total_length, output->get());

  if (!WriteFrameHeader(payload_length, HttpFrameType::SETTINGS, &writer)) {
    QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                        "settings frame header.";
    return 0;
  }

  for (auto it = settings.values.begin(); it != settings.values.end(); ++it) {
    if (!writer.WriteVarInt62(it->first) || !writer.WriteVarInt62(it->second)) {
      QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                          "settings frame payload.";
      return 0;
    }
  }

  return total_length;
}

// static
QuicByteCount HttpEncoder::SerializePushPromiseFrameWithOnlyPushId(
    const PushPromiseFrame& push_promise,
    std::unique_ptr<char[]>* output) {
  QuicByteCount payload_length =
      QuicDataWriter::GetVarInt62Len(push_promise.push_id) +
      push_promise.headers.length();
  // GetTotalLength() is not used because headers will not be serialized.
  QuicByteCount total_length =
      QuicDataWriter::GetVarInt62Len(payload_length) +
      QuicDataWriter::GetVarInt62Len(
          static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE)) +
      QuicDataWriter::GetVarInt62Len(push_promise.push_id);

  output->reset(new char[total_length]);
  QuicDataWriter writer(total_length, output->get());

  if (WriteFrameHeader(payload_length, HttpFrameType::PUSH_PROMISE, &writer) &&
      writer.WriteVarInt62(push_promise.push_id)) {
    return total_length;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize push promise frame.";
  return 0;
}

// static
QuicByteCount HttpEncoder::SerializeGoAwayFrame(
    const GoAwayFrame& goaway,
    std::unique_ptr<char[]>* output) {
  QuicByteCount payload_length =
      QuicDataWriter::GetVarInt62Len(goaway.stream_id);
  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::GOAWAY);

  output->reset(new char[total_length]);
  QuicDataWriter writer(total_length, output->get());

  if (WriteFrameHeader(payload_length, HttpFrameType::GOAWAY, &writer) &&
      writer.WriteVarInt62(goaway.stream_id)) {
    return total_length;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize goaway frame.";
  return 0;
}

// static
QuicByteCount HttpEncoder::SerializeMaxPushIdFrame(
    const MaxPushIdFrame& max_push_id,
    std::unique_ptr<char[]>* output) {
  QuicByteCount payload_length =
      QuicDataWriter::GetVarInt62Len(max_push_id.push_id);
  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::MAX_PUSH_ID);

  output->reset(new char[total_length]);
  QuicDataWriter writer(total_length, output->get());

  if (WriteFrameHeader(payload_length, HttpFrameType::MAX_PUSH_ID, &writer) &&
      writer.WriteVarInt62(max_push_id.push_id)) {
    return total_length;
  }
  QUIC_DLOG(ERROR)
      << "Http encoder failed when attempting to serialize max push id frame.";
  return 0;
}

// static
QuicByteCount HttpEncoder::SerializeDuplicatePushFrame(
    const DuplicatePushFrame& duplicate_push,
    std::unique_ptr<char[]>* output) {
  QuicByteCount payload_length =
      QuicDataWriter::GetVarInt62Len(duplicate_push.push_id);
  QuicByteCount total_length =
      GetTotalLength(payload_length, HttpFrameType::DUPLICATE_PUSH);

  output->reset(new char[total_length]);
  QuicDataWriter writer(total_length, output->get());

  if (WriteFrameHeader(payload_length, HttpFrameType::DUPLICATE_PUSH,
                       &writer) &&
      writer.WriteVarInt62(duplicate_push.push_id)) {
    return total_length;
  }
  QUIC_DLOG(ERROR) << "Http encoder failed when attempting to serialize "
                      "duplicate push frame.";
  return 0;
}

}  // namespace quic
