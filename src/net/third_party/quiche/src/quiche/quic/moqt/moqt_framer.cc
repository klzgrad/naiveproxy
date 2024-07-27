// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_framer.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/common/wire_serialization.h"

namespace moqt {

namespace {

using ::quiche::QuicheBuffer;
using ::quiche::WireBytes;
using ::quiche::WireOptional;
using ::quiche::WireSpan;
using ::quiche::WireStringWithVarInt62Length;
using ::quiche::WireUint8;
using ::quiche::WireVarInt62;

// Encoding for MOQT Locations:
// https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#name-subscribe-locations
class WireLocation {
 public:
  using DataType = std::optional<MoqtSubscribeLocation>;
  explicit WireLocation(const DataType& location) : location_(location) {}

  size_t GetLengthOnWire() {
    return quiche::ComputeLengthOnWire(
        WireVarInt62(GetModeForSubscribeLocation(location_)),
        WireOptional<WireVarInt62>(LocationOffsetOnTheWire(location_)));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    return quiche::SerializeIntoWriter(
        writer, WireVarInt62(GetModeForSubscribeLocation(location_)),
        WireOptional<WireVarInt62>(LocationOffsetOnTheWire(location_)));
  }

 private:
  // For all location types other than None, we record a single varint after the
  // type; this function computes the value of that varint.
  static std::optional<uint64_t> LocationOffsetOnTheWire(
      std::optional<MoqtSubscribeLocation> location) {
    if (!location.has_value()) {
      return std::nullopt;
    }
    if (location->absolute) {
      return location->absolute_value;
    }
    return location->relative_value <= 0 ? -location->relative_value
                                         : location->relative_value + 1;
  }

  const DataType& location_;
};

// Encoding for string parameters as described in
// https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#name-parameters
struct StringParameter {
  template <typename Enum>
  StringParameter(Enum type, absl::string_view data)
      : type(static_cast<uint64_t>(type)), data(data) {
    static_assert(std::is_enum_v<Enum>);
  }

  uint64_t type;
  absl::string_view data;
};
class WireStringParameter {
 public:
  using DataType = StringParameter;

  explicit WireStringParameter(const StringParameter& parameter)
      : parameter_(parameter) {}
  size_t GetLengthOnWire() {
    return quiche::ComputeLengthOnWire(
        WireVarInt62(parameter_.type),
        WireStringWithVarInt62Length(parameter_.data));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    return quiche::SerializeIntoWriter(
        writer, WireVarInt62(parameter_.type),
        WireStringWithVarInt62Length(parameter_.data));
  }

 private:
  const StringParameter& parameter_;
};

// Encoding for integer parameters as described in
// https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#name-parameters
struct IntParameter {
  template <typename Enum, typename Param>
  IntParameter(Enum type, Param value)
      : type(static_cast<uint64_t>(type)), value(static_cast<uint64_t>(value)) {
    static_assert(std::is_enum_v<Enum>);
    static_assert(std::is_enum_v<Param> || std::is_unsigned_v<Param>);
  }

  uint64_t type;
  uint64_t value;
};
class WireIntParameter {
 public:
  using DataType = IntParameter;

  explicit WireIntParameter(const IntParameter& parameter)
      : parameter_(parameter) {}
  size_t GetLengthOnWire() {
    return quiche::ComputeLengthOnWire(
        WireVarInt62(parameter_.type),
        WireVarInt62(NeededVarIntLen(parameter_.value)),
        WireVarInt62(parameter_.value));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    return quiche::SerializeIntoWriter(
        writer, WireVarInt62(parameter_.type),
        WireVarInt62(NeededVarIntLen(parameter_.value)),
        WireVarInt62(parameter_.value));
  }

 private:
  size_t NeededVarIntLen(const uint64_t value) {
    return static_cast<size_t>(quic::QuicDataWriter::GetVarInt62Len(value));
  }

  const IntParameter& parameter_;
};

// Serializes data into buffer using the default allocator.  Invokes QUICHE_BUG
// on failure.
template <typename... Ts>
QuicheBuffer Serialize(Ts... data) {
  absl::StatusOr<QuicheBuffer> buffer = quiche::SerializeIntoBuffer(
      quiche::SimpleBufferAllocator::Get(), data...);
  if (!buffer.ok()) {
    QUICHE_BUG(moqt_failed_serialization)
        << "Failed to serialize MoQT frame: " << buffer.status();
    return QuicheBuffer();
  }
  return *std::move(buffer);
}

}  // namespace

quiche::QuicheBuffer MoqtFramer::SerializeObjectHeader(
    const MoqtObject& message, bool is_first_in_stream) {
  if (!message.payload_length.has_value() &&
      !(message.forwarding_preference == MoqtForwardingPreference::kObject ||
        message.forwarding_preference == MoqtForwardingPreference::kDatagram)) {
    QUIC_BUG(quic_bug_serialize_object_input_01)
        << "Track or Group forwarding preference requires knowing the object "
           "length in advance";
    return quiche::QuicheBuffer();
  }
  if (!is_first_in_stream) {
    switch (message.forwarding_preference) {
      case MoqtForwardingPreference::kTrack:
        return Serialize(WireVarInt62(message.group_id),
                         WireVarInt62(message.object_id),
                         WireVarInt62(*message.payload_length));
      case MoqtForwardingPreference::kGroup:
        return Serialize(WireVarInt62(message.object_id),
                         WireVarInt62(*message.payload_length));
      default:
        QUIC_BUG(quic_bug_serialize_object_input_02)
            << "Object or Datagram forwarding_preference must be first in "
               "stream";
        return quiche::QuicheBuffer();
    }
  }
  MoqtMessageType message_type =
      GetMessageTypeForForwardingPreference(message.forwarding_preference);
  switch (message.forwarding_preference) {
    case MoqtForwardingPreference::kTrack:
      return Serialize(
          WireVarInt62(message_type), WireVarInt62(message.subscribe_id),
          WireVarInt62(message.track_alias),
          WireVarInt62(message.object_send_order),
          WireVarInt62(message.group_id), WireVarInt62(message.object_id),
          WireVarInt62(*message.payload_length));
    case MoqtForwardingPreference::kGroup:
      return Serialize(
          WireVarInt62(message_type), WireVarInt62(message.subscribe_id),
          WireVarInt62(message.track_alias), WireVarInt62(message.group_id),
          WireVarInt62(message.object_send_order),
          WireVarInt62(message.object_id),
          WireVarInt62(*message.payload_length));
    case MoqtForwardingPreference::kObject:
    case MoqtForwardingPreference::kDatagram:
      return Serialize(
          WireVarInt62(message_type), WireVarInt62(message.subscribe_id),
          WireVarInt62(message.track_alias), WireVarInt62(message.group_id),
          WireVarInt62(message.object_id),
          WireVarInt62(message.object_send_order));
  }
}

quiche::QuicheBuffer MoqtFramer::SerializeObjectDatagram(
    const MoqtObject& message, absl::string_view payload) {
  return Serialize(
      WireVarInt62(MoqtMessageType::kObjectDatagram),
      WireVarInt62(message.subscribe_id), WireVarInt62(message.track_alias),
      WireVarInt62(message.group_id), WireVarInt62(message.object_id),
      WireVarInt62(message.object_send_order), WireBytes(payload));
}

quiche::QuicheBuffer MoqtFramer::SerializeClientSetup(
    const MoqtClientSetup& message) {
  absl::InlinedVector<IntParameter, 1> int_parameters;
  absl::InlinedVector<StringParameter, 1> string_parameters;
  if (message.role.has_value()) {
    int_parameters.push_back(
        IntParameter(MoqtSetupParameter::kRole, *message.role));
  }
  if (!using_webtrans_ && message.path.has_value()) {
    string_parameters.push_back(
        StringParameter(MoqtSetupParameter::kPath, *message.path));
  }
  return Serialize(
      WireVarInt62(MoqtMessageType::kClientSetup),
      WireVarInt62(message.supported_versions.size()),
      WireSpan<WireVarInt62, MoqtVersion>(message.supported_versions),
      WireVarInt62(string_parameters.size() + int_parameters.size()),
      WireSpan<WireIntParameter>(int_parameters),
      WireSpan<WireStringParameter>(string_parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeServerSetup(
    const MoqtServerSetup& message) {
  absl::InlinedVector<IntParameter, 1> int_parameters;
  if (message.role.has_value()) {
    int_parameters.push_back(
        IntParameter(MoqtSetupParameter::kRole, *message.role));
  }
  return Serialize(WireVarInt62(MoqtMessageType::kServerSetup),
                   WireVarInt62(message.selected_version),
                   WireVarInt62(int_parameters.size()),
                   WireSpan<WireIntParameter>(int_parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribe(
    const MoqtSubscribe& message) {
  if (!message.start_group.has_value() || !message.start_object.has_value()) {
    QUICHE_BUG(MoqtFramer_start_group_missing)
        << "start_group or start_object is missing";
    return quiche::QuicheBuffer();
  }
  if (message.end_group.has_value() != message.end_object.has_value()) {
    QUICHE_BUG(MoqtFramer_end_mismatch)
        << "end_group and end_object must both be None or both non-None";
    return quiche::QuicheBuffer();
  }
  absl::InlinedVector<StringParameter, 1> string_params;
  if (message.authorization_info.has_value()) {
    string_params.push_back(
        StringParameter(MoqtTrackRequestParameter::kAuthorizationInfo,
                        *message.authorization_info));
  }
  return Serialize(
      WireVarInt62(MoqtMessageType::kSubscribe),
      WireVarInt62(message.subscribe_id), WireVarInt62(message.track_alias),
      WireStringWithVarInt62Length(message.track_namespace),
      WireStringWithVarInt62Length(message.track_name),
      WireLocation(message.start_group), WireLocation(message.start_object),
      WireLocation(message.end_group), WireLocation(message.end_object),
      WireVarInt62(string_params.size()),
      WireSpan<WireStringParameter>(string_params));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeOk(
    const MoqtSubscribeOk& message) {
  if (message.largest_id.has_value()) {
    return Serialize(WireVarInt62(MoqtMessageType::kSubscribeOk),
                     WireVarInt62(message.subscribe_id),
                     WireVarInt62(message.expires.ToMilliseconds()),
                     WireUint8(1), WireVarInt62(message.largest_id->group),
                     WireVarInt62(message.largest_id->object));
  }
  return Serialize(WireVarInt62(MoqtMessageType::kSubscribeOk),
                   WireVarInt62(message.subscribe_id),
                   WireVarInt62(message.expires.ToMilliseconds()),
                   WireUint8(0));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeError(
    const MoqtSubscribeError& message) {
  return Serialize(WireVarInt62(MoqtMessageType::kSubscribeError),
                   WireVarInt62(message.subscribe_id),
                   WireVarInt62(message.error_code),
                   WireStringWithVarInt62Length(message.reason_phrase),
                   WireVarInt62(message.track_alias));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnsubscribe(
    const MoqtUnsubscribe& message) {
  return Serialize(WireVarInt62(MoqtMessageType::kUnsubscribe),
                   WireVarInt62(message.subscribe_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeDone(
    const MoqtSubscribeDone& message) {
  if (message.final_id.has_value()) {
    return Serialize(WireVarInt62(MoqtMessageType::kSubscribeDone),
                     WireVarInt62(message.subscribe_id),
                     WireVarInt62(message.status_code),
                     WireStringWithVarInt62Length(message.reason_phrase),
                     WireUint8(1), WireVarInt62(message.final_id->group),
                     WireVarInt62(message.final_id->object));
  }
  return Serialize(
      WireVarInt62(MoqtMessageType::kSubscribeDone),
      WireVarInt62(message.subscribe_id), WireVarInt62(message.status_code),
      WireStringWithVarInt62Length(message.reason_phrase), WireUint8(0));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounce(
    const MoqtAnnounce& message) {
  absl::InlinedVector<StringParameter, 1> string_params;
  if (message.authorization_info.has_value()) {
    string_params.push_back(
        StringParameter(MoqtTrackRequestParameter::kAuthorizationInfo,
                        *message.authorization_info));
  }
  return Serialize(
      WireVarInt62(static_cast<uint64_t>(MoqtMessageType::kAnnounce)),
      WireStringWithVarInt62Length(message.track_namespace),
      WireVarInt62(string_params.size()),
      WireSpan<WireStringParameter>(string_params));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceOk(
    const MoqtAnnounceOk& message) {
  return Serialize(WireVarInt62(MoqtMessageType::kAnnounceOk),
                   WireStringWithVarInt62Length(message.track_namespace));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceError(
    const MoqtAnnounceError& message) {
  return Serialize(WireVarInt62(MoqtMessageType::kAnnounceError),
                   WireStringWithVarInt62Length(message.track_namespace),
                   WireVarInt62(message.error_code),
                   WireStringWithVarInt62Length(message.reason_phrase));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnannounce(
    const MoqtUnannounce& message) {
  return Serialize(WireVarInt62(MoqtMessageType::kUnannounce),
                   WireStringWithVarInt62Length(message.track_namespace));
}

quiche::QuicheBuffer MoqtFramer::SerializeGoAway(const MoqtGoAway& message) {
  return Serialize(WireVarInt62(MoqtMessageType::kGoAway),
                   WireStringWithVarInt62Length(message.new_session_uri));
}

}  // namespace moqt
