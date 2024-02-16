// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_framer.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace moqt {

namespace {

inline size_t NeededVarIntLen(const uint64_t value) {
  return static_cast<size_t>(quic::QuicDataWriter::GetVarInt62Len(value));
}
inline size_t NeededVarIntLen(const MoqtVersion value) {
  return static_cast<size_t>(
      quic::QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(value)));
}
inline size_t NeededVarIntLen(const MoqtMessageType value) {
  return static_cast<size_t>(
      quic::QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(value)));
}
inline size_t NeededVarIntLen(const MoqtSubscribeLocationMode value) {
  return static_cast<size_t>(
      quic::QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(value)));
}
inline size_t ParameterLen(const uint64_t type, const uint64_t value_len) {
  return NeededVarIntLen(type) + NeededVarIntLen(value_len) + value_len;
}
inline size_t LocationLength(const std::optional<MoqtSubscribeLocation> loc) {
  if (!loc.has_value()) {
    return NeededVarIntLen(MoqtSubscribeLocationMode::kNone);
  }
  if (loc->absolute) {
    return NeededVarIntLen(MoqtSubscribeLocationMode::kAbsolute) +
           NeededVarIntLen(loc->absolute_value);
  }
  // It's a relative value
  if (loc->relative_value < 0) {
    return NeededVarIntLen(MoqtSubscribeLocationMode::kRelativePrevious) +
           NeededVarIntLen(static_cast<uint64_t>(loc->relative_value * -1));
  }
  return NeededVarIntLen(MoqtSubscribeLocationMode::kRelativeNext) +
         NeededVarIntLen(static_cast<uint64_t>(loc->relative_value));
}
inline size_t LengthPrefixedStringLength(absl::string_view string) {
  return NeededVarIntLen(string.length()) + string.length();
}

// This only supports values up to UINT8_MAX, as that's all that exists in the
// standard.
inline bool WriteVarIntParameter(quic::QuicDataWriter& writer, uint64_t type,
                                 uint64_t value) {
  if (!writer.WriteVarInt62(type)) {
    return false;
  }
  if (!writer.WriteVarInt62(NeededVarIntLen(value))) {
    return false;
  }
  return writer.WriteVarInt62(value);
}

inline bool WriteStringParameter(quic::QuicDataWriter& writer, uint64_t type,
                                 absl::string_view value) {
  if (!writer.WriteVarInt62(type)) {
    return false;
  }
  return writer.WriteStringPieceVarInt62(value);
}

inline bool WriteLocation(quic::QuicDataWriter& writer,
                          std::optional<MoqtSubscribeLocation> loc) {
  if (!loc.has_value()) {
    return writer.WriteVarInt62(
        static_cast<uint64_t>(MoqtSubscribeLocationMode::kNone));
  }
  if (loc->absolute) {
    if (!writer.WriteVarInt62(
            static_cast<uint64_t>(MoqtSubscribeLocationMode::kAbsolute))) {
      return false;
    }
    return writer.WriteVarInt62(loc->absolute_value);
  }
  if (loc->relative_value < 0) {
    if (!writer.WriteVarInt62(static_cast<uint64_t>(
            MoqtSubscribeLocationMode::kRelativePrevious))) {
      return false;
    }
    return writer.WriteVarInt62(
        static_cast<uint64_t>(loc->relative_value * -1));
  }
  if (!writer.WriteVarInt62(
          static_cast<uint64_t>(MoqtSubscribeLocationMode::kRelativeNext))) {
    return false;
  }
  return writer.WriteVarInt62(static_cast<uint64_t>(loc->relative_value));
}

}  // namespace

quiche::QuicheBuffer MoqtFramer::SerializeObject(
    const MoqtObject& message, const absl::string_view payload) {
  if (message.payload_length.has_value() &&
      *message.payload_length < payload.length()) {
    QUICHE_DLOG(INFO) << "payload_size is too small for payload";
    return quiche::QuicheBuffer();
  }
  uint64_t message_type =
      static_cast<uint64_t>(message.payload_length.has_value()
                                ? MoqtMessageType::kObjectWithPayloadLength
                                : MoqtMessageType::kObjectWithoutPayloadLength);
  size_t buffer_size =
      NeededVarIntLen(message_type) + NeededVarIntLen(message.track_id) +
      NeededVarIntLen(message.group_sequence) +
      NeededVarIntLen(message.object_sequence) +
      NeededVarIntLen(message.object_send_order) + payload.length();
  if (message.payload_length.has_value()) {
    buffer_size += NeededVarIntLen(*message.payload_length);
  }
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(message_type);
  writer.WriteVarInt62(message.track_id);
  writer.WriteVarInt62(message.group_sequence);
  writer.WriteVarInt62(message.object_sequence);
  writer.WriteVarInt62(message.object_send_order);
  if (message.payload_length.has_value()) {
    writer.WriteVarInt62(*message.payload_length);
  }
  writer.WriteStringPiece(payload);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeObjectPayload(
    const absl::string_view payload) {
  quiche::QuicheBuffer buffer(allocator_, payload.length());
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteStringPiece(payload);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeClientSetup(
    const MoqtClientSetup& message) {
  size_t buffer_size = NeededVarIntLen(MoqtMessageType::kClientSetup) +
                       NeededVarIntLen(message.supported_versions.size());
  for (MoqtVersion version : message.supported_versions) {
    buffer_size += NeededVarIntLen(version);
  }
  uint64_t num_params = 0;
  if (message.role.has_value()) {
    num_params++;
    buffer_size +=
        ParameterLen(static_cast<uint64_t>(MoqtSetupParameter::kRole), 1);
  }
  if (!using_webtrans_ && message.path.has_value()) {
    num_params++;
    buffer_size +=
        ParameterLen(static_cast<uint64_t>(MoqtSetupParameter::kPath),
                     message.path->length());
  }
  buffer_size += NeededVarIntLen(num_params);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kClientSetup));
  writer.WriteVarInt62(message.supported_versions.size());
  for (MoqtVersion version : message.supported_versions) {
    writer.WriteVarInt62(static_cast<uint64_t>(version));
  }
  writer.WriteVarInt62(num_params);
  if (message.role.has_value()) {
    WriteVarIntParameter(writer,
                         static_cast<uint64_t>(MoqtSetupParameter::kRole),
                         static_cast<uint64_t>(*message.role));
  }
  if (!using_webtrans_ && message.path.has_value()) {
    WriteStringParameter(writer,
                         static_cast<uint64_t>(MoqtSetupParameter::kPath),
                         *message.path);
  }
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeServerSetup(
    const MoqtServerSetup& message) {
  size_t buffer_size = NeededVarIntLen(MoqtMessageType::kServerSetup) +
                       NeededVarIntLen(message.selected_version);
  uint64_t num_params = 0;
  if (message.role.has_value()) {
    num_params++;
    buffer_size +=
        ParameterLen(static_cast<uint64_t>(MoqtSetupParameter::kRole), 1);
  }
  buffer_size += NeededVarIntLen(num_params);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kServerSetup));
  writer.WriteVarInt62(static_cast<uint64_t>(message.selected_version));
  writer.WriteVarInt62(num_params);
  if (message.role.has_value()) {
    WriteVarIntParameter(writer,
                         static_cast<uint64_t>(MoqtSetupParameter::kRole),
                         static_cast<uint64_t>(*message.role));
  }
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeRequest(
    const MoqtSubscribeRequest& message) {
  if (!message.start_group.has_value() || !message.start_object.has_value()) {
    QUIC_LOG(INFO) << "start_group or start_object is missing";
    return quiche::QuicheBuffer();
  }
  if (message.end_group.has_value() != message.end_object.has_value()) {
    QUIC_LOG(INFO) << "end_group and end_object must both be None or both "
                   << "non-None";
    return quiche::QuicheBuffer();
  }
  size_t buffer_size = NeededVarIntLen(MoqtMessageType::kSubscribeRequest) +
                       LengthPrefixedStringLength(message.track_namespace) +
                       LengthPrefixedStringLength(message.track_name) +
                       LocationLength(message.start_group) +
                       LocationLength(message.start_object) +
                       LocationLength(message.end_group) +
                       LocationLength(message.end_object);
  uint64_t num_params = 0;
  if (message.authorization_info.has_value()) {
    num_params++;
    buffer_size += ParameterLen(
        static_cast<uint64_t>(MoqtTrackRequestParameter::kAuthorizationInfo),
        message.authorization_info->length());
  }
  buffer_size += NeededVarIntLen(num_params);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(
      static_cast<uint64_t>(MoqtMessageType::kSubscribeRequest));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteStringPieceVarInt62(message.track_name);
  WriteLocation(writer, message.start_group);
  WriteLocation(writer, message.start_object);
  WriteLocation(writer, message.end_group);
  WriteLocation(writer, message.end_object);
  writer.WriteVarInt62(num_params);
  if (message.authorization_info.has_value()) {
    WriteStringParameter(
        writer,
        static_cast<uint64_t>(MoqtTrackRequestParameter::kAuthorizationInfo),
        *message.authorization_info);
  }
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeOk(
    const MoqtSubscribeOk& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kSubscribeOk)) +
      LengthPrefixedStringLength(message.track_namespace) +
      LengthPrefixedStringLength(message.track_name) +
      NeededVarIntLen(message.track_id) +
      NeededVarIntLen(message.expires.ToMilliseconds());
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kSubscribeOk));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteStringPieceVarInt62(message.track_name);
  writer.WriteVarInt62(message.track_id);
  writer.WriteVarInt62(message.expires.ToMilliseconds());
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeError(
    const MoqtSubscribeError& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kSubscribeError)) +
      LengthPrefixedStringLength(message.track_namespace) +
      LengthPrefixedStringLength(message.track_name) +
      NeededVarIntLen(message.error_code) +
      LengthPrefixedStringLength(message.reason_phrase);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kSubscribeError));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteStringPieceVarInt62(message.track_name);
  writer.WriteVarInt62(message.error_code);
  writer.WriteStringPieceVarInt62(message.reason_phrase);
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeUnsubscribe(
    const MoqtUnsubscribe& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kUnsubscribe)) +
      LengthPrefixedStringLength(message.track_namespace) +
      LengthPrefixedStringLength(message.track_name);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kUnsubscribe));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteStringPieceVarInt62(message.track_name);
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeFin(
    const MoqtSubscribeFin& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kSubscribeFin)) +
      LengthPrefixedStringLength(message.track_namespace) +
      LengthPrefixedStringLength(message.track_name) +
      NeededVarIntLen(message.final_group) +
      NeededVarIntLen(message.final_object);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kSubscribeFin));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteStringPieceVarInt62(message.track_name);
  writer.WriteVarInt62(message.final_group);
  writer.WriteVarInt62(message.final_object);
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeRst(
    const MoqtSubscribeRst& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kSubscribeRst)) +
      LengthPrefixedStringLength(message.track_namespace) +
      LengthPrefixedStringLength(message.track_name) +
      NeededVarIntLen(message.error_code) +
      LengthPrefixedStringLength(message.reason_phrase) +
      NeededVarIntLen(message.final_group) +
      NeededVarIntLen(message.final_object);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kSubscribeRst));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteStringPieceVarInt62(message.track_name);
  writer.WriteVarInt62(message.error_code);
  writer.WriteStringPieceVarInt62(message.reason_phrase);
  writer.WriteVarInt62(message.final_group);
  writer.WriteVarInt62(message.final_object);
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounce(
    const MoqtAnnounce& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kAnnounce)) +
      LengthPrefixedStringLength(message.track_namespace);
  uint64_t num_params = 0;
  if (message.authorization_info.has_value()) {
    num_params++;
    buffer_size += ParameterLen(
        static_cast<uint64_t>(MoqtTrackRequestParameter::kAuthorizationInfo),
        message.authorization_info->length());
  }
  buffer_size += NeededVarIntLen(num_params);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kAnnounce));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteVarInt62(num_params);
  if (message.authorization_info.has_value()) {
    WriteStringParameter(
        writer,
        static_cast<uint64_t>(MoqtTrackRequestParameter::kAuthorizationInfo),
        *message.authorization_info);
  }
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceOk(
    const MoqtAnnounceOk& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kAnnounceOk)) +
      LengthPrefixedStringLength(message.track_namespace);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kAnnounceOk));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceError(
    const MoqtAnnounceError& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kAnnounceError)) +
      LengthPrefixedStringLength(message.track_namespace) +
      NeededVarIntLen(message.error_code) +
      LengthPrefixedStringLength(message.reason_phrase);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kAnnounceError));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteVarInt62(message.error_code);
  writer.WriteStringPieceVarInt62(message.reason_phrase);
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeUnannounce(
    const MoqtUnannounce& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kUnannounce)) +
      LengthPrefixedStringLength(message.track_namespace);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kUnannounce));
  writer.WriteStringPieceVarInt62(message.track_namespace);
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeGoAway(const MoqtGoAway& message) {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kGoAway)) +
      LengthPrefixedStringLength(message.new_session_uri);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kGoAway));
  writer.WriteStringPieceVarInt62(message.new_session_uri);
  QUICHE_DCHECK(writer.remaining() == 0);
  return buffer;
}

}  // namespace moqt
