// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_framer.h"

#include <cstddef>
#include <cstdint>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace moqt {

namespace {

inline size_t NeededVarIntLen(uint64_t value) {
  return static_cast<size_t>(quic::QuicDataWriter::GetVarInt62Len(value));
}
inline size_t NeededVarIntLen(MoqtVersion value) {
  return static_cast<size_t>(
      quic::QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(value)));
}
inline size_t ParameterLen(uint64_t type, uint64_t value_len) {
  return NeededVarIntLen(type) + NeededVarIntLen(value_len) + value_len;
}

// This only supports values up to UINT8_MAX, as that's all that exists in the
// standard.
inline bool WriteIntParameter(quic::QuicDataWriter& writer, uint64_t type,
                              uint8_t value) {
  if (!writer.WriteVarInt62(type)) {
    return false;
  }
  if (!writer.WriteVarInt62(1)) {
    return false;
  }
  return writer.WriteUInt8(value);
}

inline bool WriteStringParameter(quic::QuicDataWriter& writer, uint64_t type,
                                 absl::string_view value) {
  if (!writer.WriteVarInt62(type)) {
    return false;
  }
  return writer.WriteStringPieceVarInt62(value);
}

}  // namespace

quiche::QuicheBuffer MoqtFramer::SerializeObject(
    const MoqtObject& message, const absl::string_view payload,
    const size_t known_payload_size) {
  if (known_payload_size > 0 && known_payload_size < payload.length()) {
    return quiche::QuicheBuffer();
  }
  size_t varint_len = NeededVarIntLen(message.track_id) +
                      NeededVarIntLen(message.group_sequence) +
                      NeededVarIntLen(message.object_sequence) +
                      NeededVarIntLen(message.object_send_order);
  size_t message_len =
      known_payload_size == 0 ? 0 : (known_payload_size + varint_len);
  size_t buffer_size =
      varint_len + payload.length() +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kObject)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kObject));
  writer.WriteVarInt62(message_len);
  writer.WriteVarInt62(message.track_id);
  writer.WriteVarInt62(message.group_sequence);
  writer.WriteVarInt62(message.object_sequence);
  writer.WriteVarInt62(message.object_send_order);
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

quiche::QuicheBuffer MoqtFramer::SerializeSetup(const MoqtSetup& message) {
  size_t message_len;
  if (perspective_ == quic::Perspective::IS_CLIENT) {
    message_len = NeededVarIntLen(message.supported_versions.size());
    for (MoqtVersion version : message.supported_versions) {
      message_len += NeededVarIntLen(version);
    }
    // TODO: figure out if the role needs to be sent on the client side or on
    // both sides.
    if (message.role.has_value()) {
      message_len +=
          ParameterLen(static_cast<uint64_t>(MoqtSetupParameter::kRole), 1);
    }
    if (!using_webtrans_ && message.path.has_value()) {
      message_len +=
          ParameterLen(static_cast<uint64_t>(MoqtSetupParameter::kPath),
                       message.path->length());
    }
  } else {
    message_len = NeededVarIntLen(message.supported_versions[0]);
  }
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kSetup)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kSetup));
  writer.WriteVarInt62(message_len);
  if (perspective_ == quic::Perspective::IS_SERVER) {
    writer.WriteVarInt62(static_cast<uint64_t>(message.supported_versions[0]));
    return buffer;
  }
  writer.WriteVarInt62(message.supported_versions.size());
  for (MoqtVersion version : message.supported_versions) {
    writer.WriteVarInt62(static_cast<uint64_t>(version));
  }
  if (message.role.has_value()) {
    WriteIntParameter(writer, static_cast<uint64_t>(MoqtSetupParameter::kRole),
                      static_cast<uint8_t>(message.role.value()));
  }
  if (!using_webtrans_ && message.path.has_value()) {
    WriteStringParameter(writer,
                         static_cast<uint64_t>(MoqtSetupParameter::kPath),
                         message.path.value());
  }
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeRequest(
    const MoqtSubscribeRequest& message) {
  size_t message_len = NeededVarIntLen(message.full_track_name.length()) +
                       message.full_track_name.length();
  if (message.group_sequence.has_value()) {
    message_len += ParameterLen(
        static_cast<uint64_t>(MoqtTrackRequestParameter::kGroupSequence), 1);
  }
  if (message.object_sequence.has_value()) {
    message_len += ParameterLen(
        static_cast<uint64_t>(MoqtTrackRequestParameter::kObjectSequence), 1);
  }
  if (message.authorization_info.has_value()) {
    message_len += ParameterLen(
        static_cast<uint64_t>(MoqtTrackRequestParameter::kAuthorizationInfo),
        message.authorization_info->length());
  }
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kObject)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(
      static_cast<uint64_t>(MoqtMessageType::kSubscribeRequest));
  writer.WriteVarInt62(message_len);
  writer.WriteStringPieceVarInt62(message.full_track_name);
  if (message.group_sequence.has_value()) {
    WriteIntParameter(
        writer,
        static_cast<uint64_t>(MoqtTrackRequestParameter::kGroupSequence),
        message.group_sequence.value());
  }
  if (message.object_sequence.has_value()) {
    WriteIntParameter(
        writer,
        static_cast<uint64_t>(MoqtTrackRequestParameter::kObjectSequence),
        message.object_sequence.value());
  }
  if (message.authorization_info.has_value()) {
    WriteStringParameter(
        writer,
        static_cast<uint64_t>(MoqtTrackRequestParameter::kAuthorizationInfo),
        message.authorization_info.value());
  }
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeOk(
    const MoqtSubscribeOk& message) {
  size_t message_len = NeededVarIntLen(message.full_track_name.length()) +
                       message.full_track_name.length() +
                       NeededVarIntLen(message.track_id) +
                       NeededVarIntLen(message.expires.ToMilliseconds());
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kSubscribeOk)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kSubscribeOk));
  writer.WriteVarInt62(message_len);
  writer.WriteStringPieceVarInt62(message.full_track_name);
  writer.WriteVarInt62(message.track_id);
  writer.WriteVarInt62(message.expires.ToMilliseconds());
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeError(
    const MoqtSubscribeError& message) {
  size_t message_len = NeededVarIntLen(message.full_track_name.length()) +
                       message.full_track_name.length() +
                       NeededVarIntLen(message.error_code) +
                       NeededVarIntLen(message.reason_phrase.length()) +
                       message.reason_phrase.length();
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kSubscribeError)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kSubscribeError));
  writer.WriteVarInt62(message_len);
  writer.WriteStringPieceVarInt62(message.full_track_name);
  writer.WriteVarInt62(message.error_code);
  writer.WriteStringPieceVarInt62(message.reason_phrase);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeUnsubscribe(
    const MoqtUnsubscribe& message) {
  size_t message_len = NeededVarIntLen(message.full_track_name.length()) +
                       message.full_track_name.length();
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kUnsubscribe)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kUnsubscribe));
  writer.WriteVarInt62(message_len);
  writer.WriteStringPieceVarInt62(message.full_track_name);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounce(
    const MoqtAnnounce& message) {
  size_t message_len = NeededVarIntLen(message.track_namespace.length()) +
                       message.track_namespace.length();
  if (message.authorization_info.has_value()) {
    message_len += ParameterLen(
        static_cast<uint64_t>(MoqtTrackRequestParameter::kAuthorizationInfo),
        message.authorization_info->length());
  }
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kAnnounce)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kAnnounce));
  writer.WriteVarInt62(message_len);
  writer.WriteStringPieceVarInt62(message.track_namespace);
  if (message.authorization_info.has_value()) {
    WriteStringParameter(
        writer,
        static_cast<uint64_t>(MoqtTrackRequestParameter::kAuthorizationInfo),
        message.authorization_info.value());
  }
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceOk(
    const MoqtAnnounceOk& message) {
  size_t message_len = NeededVarIntLen(message.track_namespace.length()) +
                       message.track_namespace.length();
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kAnnounceOk)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kAnnounceOk));
  writer.WriteVarInt62(message_len);
  writer.WriteStringPieceVarInt62(message.track_namespace);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceError(
    const MoqtAnnounceError& message) {
  size_t message_len = NeededVarIntLen(message.track_namespace.length()) +
                       message.track_namespace.length() +
                       NeededVarIntLen(message.error_code) +
                       NeededVarIntLen(message.reason_phrase.length()) +
                       message.reason_phrase.length();
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kAnnounceError)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kAnnounceError));
  writer.WriteVarInt62(message_len);
  writer.WriteStringPieceVarInt62(message.track_namespace);
  writer.WriteVarInt62(message.error_code);
  writer.WriteStringPieceVarInt62(message.reason_phrase);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeUnannounce(
    const MoqtUnannounce& message) {
  size_t message_len = NeededVarIntLen(message.track_namespace.length()) +
                       message.track_namespace.length();
  size_t buffer_size =
      message_len +
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kUnannounce)) +
      NeededVarIntLen(message_len);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kUnannounce));
  writer.WriteVarInt62(message_len);
  writer.WriteStringPieceVarInt62(message.track_namespace);
  return buffer;
}

quiche::QuicheBuffer MoqtFramer::SerializeGoAway() {
  size_t buffer_size =
      NeededVarIntLen(static_cast<uint64_t>(MoqtMessageType::kGoAway)) +
      NeededVarIntLen(0);
  quiche::QuicheBuffer buffer(allocator_, buffer_size);
  quic::QuicDataWriter writer(buffer.size(), buffer.data());
  writer.WriteVarInt62(static_cast<uint64_t>(MoqtMessageType::kGoAway));
  writer.WriteVarInt62(0);
  return buffer;
}

}  // namespace moqt
