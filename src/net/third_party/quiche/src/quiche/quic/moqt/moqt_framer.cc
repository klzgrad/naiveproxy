// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_framer.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
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

class WireKeyVarIntPair {
 public:
  explicit WireKeyVarIntPair(uint64_t key, uint64_t value)
      : key_(key), value_(value) {}

  size_t GetLengthOnWire() {
    return quiche::ComputeLengthOnWire(WireVarInt62(key_),
                                       WireVarInt62(value_));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    return quiche::SerializeIntoWriter(writer, WireVarInt62(key_),
                                       WireVarInt62(value_));
  }

 private:
  const uint64_t key_;
  const uint64_t value_;
};

class WireKeyStringPair {
 public:
  explicit WireKeyStringPair(uint64_t key, absl::string_view value)
      : key_(key), value_(value) {}
  size_t GetLengthOnWire() {
    return quiche::ComputeLengthOnWire(WireVarInt62(key_),
                                       WireStringWithVarInt62Length(value_));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    return quiche::SerializeIntoWriter(writer, WireVarInt62(key_),
                                       WireStringWithVarInt62Length(value_));
  }

 private:
  const uint64_t key_;
  const absl::string_view value_;
};

class WireKeyValuePairList {
 public:
  explicit WireKeyValuePairList(const KeyValuePairList& list) : list_(list) {}

  size_t GetLengthOnWire() {
    size_t total = WireVarInt62(list_.size()).GetLengthOnWire();
    list_.ForEach(
        [&](uint64_t key, uint64_t value) {
          total += WireKeyVarIntPair(key, value).GetLengthOnWire();
          return true;
        },
        [&](uint64_t key, absl::string_view value) {
          total += WireKeyStringPair(key, value).GetLengthOnWire();
          return true;
        });
    return total;
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    WireVarInt62(list_.size()).SerializeIntoWriter(writer);
    list_.ForEach(
        [&](uint64_t key, uint64_t value) {
          absl::Status status =
              WireKeyVarIntPair(key, value).SerializeIntoWriter(writer);
          return quiche::IsWriterStatusOk(status);
        },
        [&](uint64_t key, absl::string_view value) {
          absl::Status status =
              WireKeyStringPair(key, value).SerializeIntoWriter(writer);
          return quiche::IsWriterStatusOk(status);
        });
    return absl::OkStatus();
  }

 private:
  const KeyValuePairList& list_;
};

class WireTrackNamespace {
 public:
  WireTrackNamespace(const TrackNamespace& name) : namespace_(name) {}

  size_t GetLengthOnWire() {
    return quiche::ComputeLengthOnWire(
        WireVarInt62(namespace_.number_of_elements()),
        WireSpan<WireStringWithVarInt62Length, std::string>(
            namespace_.tuple()));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    return quiche::SerializeIntoWriter(
        writer, WireVarInt62(namespace_.number_of_elements()),
        WireSpan<WireStringWithVarInt62Length, std::string>(
            namespace_.tuple()));
  }

 private:
  const TrackNamespace& namespace_;
};

class WireFullTrackName {
 public:
  WireFullTrackName(const FullTrackName& name) : name_(name) {}

  size_t GetLengthOnWire() {
    return quiche::ComputeLengthOnWire(
        WireTrackNamespace(name_.track_namespace()),
        WireStringWithVarInt62Length(name_.name()));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    return quiche::SerializeIntoWriter(
        writer, WireTrackNamespace(name_.track_namespace()),
        WireStringWithVarInt62Length(name_.name()));
  }

 private:
  const FullTrackName& name_;
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

// Serializes data into buffer using the default allocator.  Invokes QUICHE_BUG
// on failure.
template <typename... Ts>
QuicheBuffer SerializeControlMessage(MoqtMessageType type, Ts... data) {
  uint64_t message_type = static_cast<uint64_t>(type);
  size_t payload_size = quiche::ComputeLengthOnWire(data...);
  size_t buffer_size = sizeof(uint16_t) + payload_size +
                       quiche::ComputeLengthOnWire(WireVarInt62(message_type));
  if (buffer_size == 0) {
    return QuicheBuffer();
  }

  QuicheBuffer buffer(quiche::SimpleBufferAllocator::Get(), buffer_size);
  quiche::QuicheDataWriter writer(buffer.size(), buffer.data());
  absl::Status status =
      SerializeIntoWriter(writer, WireVarInt62(message_type),
                          quiche::WireUint16(payload_size), data...);
  if (!status.ok() || writer.remaining() != 0) {
    QUICHE_BUG(moqt_failed_serialization)
        << "Failed to serialize MoQT frame: " << status;
    return QuicheBuffer();
  }
  return buffer;
}

WireUint8 WireDeliveryOrder(std::optional<MoqtDeliveryOrder> delivery_order) {
  if (!delivery_order.has_value()) {
    return WireUint8(0x00);
  }
  switch (*delivery_order) {
    case MoqtDeliveryOrder::kAscending:
      return WireUint8(0x01);
    case MoqtDeliveryOrder::kDescending:
      return WireUint8(0x02);
  }
  QUICHE_NOTREACHED();
  return WireUint8(0xff);
}

WireUint8 WireBoolean(bool value) { return WireUint8(value ? 0x01 : 0x00); }

uint64_t SignedVarintSerializedForm(int64_t value) {
  if (value < 0) {
    return ((-value) << 1) | 0x01;
  }
  return value << 1;
}

void SessionParametersToKeyValuePairList(
    const MoqtSessionParameters& parameters, KeyValuePairList& out) {
  if (!parameters.using_webtrans &&
      parameters.perspective == quic::Perspective::IS_CLIENT) {
    out.insert(SetupParameter::kPath, parameters.path);
  }
  if (parameters.max_request_id > 0) {
    out.insert(SetupParameter::kMaxRequestId, parameters.max_request_id);
  }
  if (parameters.max_auth_token_cache_size > 0) {
    out.insert(SetupParameter::kMaxAuthTokenCacheSize,
               parameters.max_auth_token_cache_size);
  }
  if (parameters.support_object_acks) {
    out.insert(SetupParameter::kSupportObjectAcks, 1ULL);
  }
}

void VersionSpecificParametersToKeyValuePairList(
    const VersionSpecificParameters& parameters, KeyValuePairList& out) {
  out.clear();
  for (const auto& it : parameters.authorization_token) {
    if (it.type > AuthTokenType::kMaxAuthTokenType) {
      QUICHE_BUG(moqt_invalid_auth_token_type)
          << "Invalid Auth Token Type: " << static_cast<uint64_t>(it.type);
      continue;
    }
    // Just support USE_VALUE for now.
    quiche::QuicheBuffer parameter_value =
        Serialize(WireVarInt62(AuthTokenAliasType::kUseValue),
                  WireVarInt62(it.type), WireBytes(it.token));
    out.insert(VersionSpecificParameter::kAuthorizationToken,
               std::string(parameter_value.AsStringView()));
  }
  if (!parameters.delivery_timeout.IsInfinite()) {
    out.insert(
        VersionSpecificParameter::kDeliveryTimeout,
        static_cast<uint64_t>(parameters.delivery_timeout.ToMilliseconds()));
  }
  if (!parameters.max_cache_duration.IsInfinite()) {
    out.insert(
        VersionSpecificParameter::kMaxCacheDuration,
        static_cast<uint64_t>(parameters.max_cache_duration.ToMilliseconds()));
  }
  if (parameters.oack_window_size.has_value()) {
    out.insert(
        VersionSpecificParameter::kOackWindowSize,
        static_cast<uint64_t>(parameters.oack_window_size->ToMicroseconds()));
  }
}

}  // namespace

quiche::QuicheBuffer MoqtFramer::SerializeObjectHeader(
    const MoqtObject& message, MoqtDataStreamType message_type,
    bool is_first_in_stream) {
  if (!ValidateObjectMetadata(message, /*is_datagram=*/false)) {
    QUICHE_BUG(QUICHE_BUG_serialize_object_header_01)
        << "Object metadata is invalid";
    return quiche::QuicheBuffer();
  }
  // Not all fields will be written to the wire. Keep optional ones in
  // std::optional so that they can be excluded.
  // Three fields are always optional.
  std::optional<uint64_t> stream_type =
      is_first_in_stream ? std::optional<uint64_t>(message_type.value())
                         : std::nullopt;
  std::optional<uint64_t> track_alias =
      is_first_in_stream ? std::optional<uint64_t>(message.track_alias)
                         : std::nullopt;
  std::optional<uint64_t> object_status =
      (message.payload_length == 0)
          ? std::optional<uint64_t>(
                static_cast<uint64_t>(message.object_status))
          : std::nullopt;
  if (message_type.IsFetch()) {
    return Serialize(
        WireOptional<WireVarInt62>(stream_type),
        WireOptional<WireVarInt62>(track_alias), WireVarInt62(message.group_id),
        WireVarInt62(message.subgroup_id), WireVarInt62(message.object_id),
        WireUint8(message.publisher_priority),
        WireStringWithVarInt62Length(message.extension_headers),
        WireVarInt62(message.payload_length),
        WireOptional<WireVarInt62>(object_status));
  }
  // Subgroup headers have more optional fields.
  QUICHE_CHECK(message_type.IsSubgroup());
  std::optional<uint64_t> group_id =
      is_first_in_stream ? std::optional<uint64_t>(message.group_id)
                         : std::nullopt;
  std::optional<uint64_t> subgroup_id =
      (is_first_in_stream && message_type.IsSubgroupPresent())
          ? std::optional<uint64_t>(message.subgroup_id)
          : std::nullopt;
  std::optional<uint8_t> publisher_priority =
      is_first_in_stream ? std::optional<uint8_t>(message.publisher_priority)
                         : std::nullopt;
  std::optional<absl::string_view> extension_headers =
      (message_type.AreExtensionHeadersPresent())
          ? std::optional<absl::string_view>(message.extension_headers)
          : std::nullopt;
  return Serialize(
      WireOptional<WireVarInt62>(stream_type),
      WireOptional<WireVarInt62>(track_alias),
      WireOptional<WireVarInt62>(group_id),
      WireOptional<WireVarInt62>(subgroup_id),
      WireOptional<WireUint8>(publisher_priority),
      WireVarInt62(message.object_id),
      WireOptional<WireStringWithVarInt62Length>(extension_headers),
      WireVarInt62(message.payload_length),
      WireOptional<WireVarInt62>(object_status));
}

quiche::QuicheBuffer MoqtFramer::SerializeObjectDatagram(
    const MoqtObject& message, absl::string_view payload) {
  if (!ValidateObjectMetadata(message, /*is_datagram=*/true)) {
    QUICHE_BUG(QUICHE_BUG_serialize_object_datagram_01)
        << "Object metadata is invalid";
    return quiche::QuicheBuffer();
  }
  if (message.payload_length != payload.length()) {
    QUICHE_BUG(QUICHE_BUG_serialize_object_datagram_03)
        << "Payload length does not match payload";
    return quiche::QuicheBuffer();
  }
  MoqtDatagramType datagram_type(/*has_status=*/payload.empty(),
                                 !message.extension_headers.empty());
  std::optional<absl::string_view> extensions =
      datagram_type.has_extension()
          ? std::optional<absl::string_view>(message.extension_headers)
          : std::nullopt;
  std::optional<uint64_t> object_status =
      payload.empty() ? std::optional<uint64_t>(
                            static_cast<uint64_t>(message.object_status))
                      : std::nullopt;
  std::optional<absl::string_view> raw_payload =
      payload.empty() ? std::nullopt
                      : std::optional<absl::string_view>(payload);
  return Serialize(
      WireVarInt62(datagram_type.value()), WireVarInt62(message.track_alias),
      WireVarInt62(message.group_id), WireVarInt62(message.object_id),
      WireUint8(message.publisher_priority),
      WireOptional<WireStringWithVarInt62Length>(extensions),
      WireOptional<WireVarInt62>(object_status),
      WireOptional<WireBytes>(raw_payload));
}

quiche::QuicheBuffer MoqtFramer::SerializeClientSetup(
    const MoqtClientSetup& message) {
  KeyValuePairList parameters;
  SessionParametersToKeyValuePairList(message.parameters, parameters);
  if (ValidateSetupParameters(parameters, using_webtrans_,
                              quic::Perspective::IS_SERVER) !=
      MoqtError::kNoError) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(
      MoqtMessageType::kClientSetup,
      WireVarInt62(message.supported_versions.size()),
      WireSpan<WireVarInt62, MoqtVersion>(message.supported_versions),
      WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeServerSetup(
    const MoqtServerSetup& message) {
  KeyValuePairList parameters;
  SessionParametersToKeyValuePairList(message.parameters, parameters);
  if (ValidateSetupParameters(parameters, using_webtrans_,
                              quic::Perspective::IS_CLIENT) !=
      MoqtError::kNoError) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(MoqtMessageType::kServerSetup,
                                 WireVarInt62(message.selected_version),
                                 WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribe(
    const MoqtSubscribe& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kSubscribe)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  std::optional<uint64_t> start_group, start_object, end_group;
  switch (message.filter_type) {
    case MoqtFilterType::kNextGroupStart:
    case MoqtFilterType::kLatestObject:
      break;
    case MoqtFilterType::kAbsoluteRange:
      if (!message.end_group.has_value() || !message.start.has_value() ||
          *message.end_group < message.start->group) {
        QUICHE_BUG(MoqtFramer_invalid_end_group) << "Invalid object range";
        return quiche::QuicheBuffer();
      }
      end_group = *message.end_group;
      [[fallthrough]];
    case MoqtFilterType::kAbsoluteStart:
      if (!message.start.has_value()) {
        QUICHE_BUG(MoqtFramer_invalid_start) << "Filter requires start";
        return quiche::QuicheBuffer();
      }
      start_group = message.start->group;
      start_object = message.start->object;
      break;
    default:
      QUICHE_BUG(MoqtFramer_end_group_missing) << "Subscribe framing error.";
      return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(
      MoqtMessageType::kSubscribe, WireVarInt62(message.request_id),
      WireFullTrackName(message.full_track_name),
      WireUint8(message.subscriber_priority),
      WireDeliveryOrder(message.group_order), WireBoolean(message.forward),
      WireVarInt62(message.filter_type),
      WireOptional<WireVarInt62>(start_group),
      WireOptional<WireVarInt62>(start_object),
      WireOptional<WireVarInt62>(end_group), WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeOk(
    const MoqtSubscribeOk& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kSubscribeOk)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  if (message.largest_location.has_value()) {
    return SerializeControlMessage(
        MoqtMessageType::kSubscribeOk, WireVarInt62(message.request_id),
        WireVarInt62(message.track_alias),
        WireVarInt62(message.expires.ToMilliseconds()),
        WireDeliveryOrder(message.group_order), WireUint8(1),
        WireVarInt62(message.largest_location->group),
        WireVarInt62(message.largest_location->object),
        WireKeyValuePairList(parameters));
  }
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeOk, WireVarInt62(message.request_id),
      WireVarInt62(message.track_alias),
      WireVarInt62(message.expires.ToMilliseconds()),
      WireDeliveryOrder(message.group_order), WireUint8(0),
      WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeError(
    const MoqtSubscribeError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeError, WireVarInt62(message.request_id),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.reason_phrase));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnsubscribe(
    const MoqtUnsubscribe& message) {
  return SerializeControlMessage(MoqtMessageType::kUnsubscribe,
                                 WireVarInt62(message.request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeDone(
    const MoqtSubscribeDone& message) {
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeDone, WireVarInt62(message.request_id),
      WireVarInt62(message.status_code), WireVarInt62(message.stream_count),
      WireStringWithVarInt62Length(message.error_reason));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeUpdate(
    const MoqtSubscribeUpdate& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kSubscribeUpdate)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  uint64_t end_group =
      message.end_group.has_value() ? *message.end_group + 1 : 0;
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeUpdate, WireVarInt62(message.request_id),
      WireVarInt62(message.start.group), WireVarInt62(message.start.object),
      WireVarInt62(end_group), WireUint8(message.subscriber_priority),
      WireBoolean(message.forward), WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounce(
    const MoqtAnnounce& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kAnnounce)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(MoqtMessageType::kAnnounce,
                                 WireVarInt62(message.request_id),
                                 WireTrackNamespace(message.track_namespace),
                                 WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceOk(
    const MoqtAnnounceOk& message) {
  return SerializeControlMessage(MoqtMessageType::kAnnounceOk,
                                 WireVarInt62(message.request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceError(
    const MoqtAnnounceError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kAnnounceError, WireVarInt62(message.request_id),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.error_reason));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnannounce(
    const MoqtUnannounce& message) {
  return SerializeControlMessage(MoqtMessageType::kUnannounce,
                                 WireTrackNamespace(message.track_namespace));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceCancel(
    const MoqtAnnounceCancel& message) {
  return SerializeControlMessage(
      MoqtMessageType::kAnnounceCancel,
      WireTrackNamespace(message.track_namespace),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.error_reason));
}

quiche::QuicheBuffer MoqtFramer::SerializeTrackStatusRequest(
    const MoqtTrackStatusRequest& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(
          parameters, MoqtMessageType::kTrackStatusRequest)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(MoqtMessageType::kTrackStatusRequest,
                                 WireVarInt62(message.request_id),
                                 WireFullTrackName(message.full_track_name),
                                 WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeTrackStatus(
    const MoqtTrackStatus& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kTrackStatus)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(MoqtMessageType::kTrackStatus,
                                 WireVarInt62(message.request_id),
                                 WireVarInt62(message.status_code),
                                 WireVarInt62(message.largest_location.group),
                                 WireVarInt62(message.largest_location.object),
                                 WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeGoAway(const MoqtGoAway& message) {
  return SerializeControlMessage(
      MoqtMessageType::kGoAway,
      WireStringWithVarInt62Length(message.new_session_uri));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeAnnounces(
    const MoqtSubscribeAnnounces& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(
          parameters, MoqtMessageType::kSubscribeAnnounces)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(MoqtMessageType::kSubscribeAnnounces,
                                 WireVarInt62(message.request_id),
                                 WireTrackNamespace(message.track_namespace),
                                 WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeAnnouncesOk(
    const MoqtSubscribeAnnouncesOk& message) {
  return SerializeControlMessage(MoqtMessageType::kSubscribeAnnouncesOk,
                                 WireVarInt62(message.request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeAnnouncesError(
    const MoqtSubscribeAnnouncesError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeAnnouncesError,
      WireVarInt62(message.request_id), WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.error_reason));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnsubscribeAnnounces(
    const MoqtUnsubscribeAnnounces& message) {
  return SerializeControlMessage(MoqtMessageType::kUnsubscribeAnnounces,
                                 WireTrackNamespace(message.track_namespace));
}

quiche::QuicheBuffer MoqtFramer::SerializeMaxRequestId(
    const MoqtMaxRequestId& message) {
  return SerializeControlMessage(MoqtMessageType::kMaxRequestId,
                                 WireVarInt62(message.max_request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeFetch(const MoqtFetch& message) {
  if (std::holds_alternative<StandaloneFetch>(message.fetch)) {
    const StandaloneFetch& standalone_fetch =
        std::get<StandaloneFetch>(message.fetch);
    if (standalone_fetch.end_group < standalone_fetch.start_object.group ||
        (standalone_fetch.end_group == standalone_fetch.start_object.group &&
         standalone_fetch.end_object.has_value() &&
         *standalone_fetch.end_object < standalone_fetch.start_object.object)) {
      QUICHE_BUG(MoqtFramer_invalid_fetch) << "Invalid FETCH object range";
      return quiche::QuicheBuffer();
    }
  }
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters, MoqtMessageType::kFetch)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  if (std::holds_alternative<StandaloneFetch>(message.fetch)) {
    const StandaloneFetch& standalone_fetch =
        std::get<StandaloneFetch>(message.fetch);
    return SerializeControlMessage(
        MoqtMessageType::kFetch, WireVarInt62(message.request_id),
        WireUint8(message.subscriber_priority),
        WireDeliveryOrder(message.group_order),
        WireVarInt62(FetchType::kStandalone),
        WireFullTrackName(standalone_fetch.full_track_name),
        WireVarInt62(standalone_fetch.start_object.group),
        WireVarInt62(standalone_fetch.start_object.object),
        WireVarInt62(standalone_fetch.end_group),
        WireVarInt62(standalone_fetch.end_object.has_value()
                         ? *standalone_fetch.end_object + 1
                         : 0),
        WireKeyValuePairList(parameters));
  }
  uint64_t subscribe_id;
  uint64_t joining_start;
  if (std::holds_alternative<JoiningFetchRelative>(message.fetch)) {
    const JoiningFetchRelative& joining_fetch =
        std::get<JoiningFetchRelative>(message.fetch);
    subscribe_id = joining_fetch.joining_subscribe_id;
    joining_start = joining_fetch.joining_start;
  } else {
    const JoiningFetchAbsolute& joining_fetch =
        std::get<JoiningFetchAbsolute>(message.fetch);
    subscribe_id = joining_fetch.joining_subscribe_id;
    joining_start = joining_fetch.joining_start;
  }
  return SerializeControlMessage(
      MoqtMessageType::kFetch, WireVarInt62(message.request_id),
      WireUint8(message.subscriber_priority),
      WireDeliveryOrder(message.group_order),
      WireVarInt62(message.fetch.index() + 1), WireVarInt62(subscribe_id),
      WireVarInt62(joining_start), WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeFetchOk(const MoqtFetchOk& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kFetchOk)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(
      MoqtMessageType::kFetchOk, WireVarInt62(message.request_id),
      WireDeliveryOrder(message.group_order), WireBoolean(message.end_of_track),
      WireVarInt62(message.end_location.group),
      WireVarInt62(message.end_location.object),
      WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeFetchError(
    const MoqtFetchError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kFetchError, WireVarInt62(message.request_id),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.error_reason));
}

quiche::QuicheBuffer MoqtFramer::SerializeFetchCancel(
    const MoqtFetchCancel& message) {
  return SerializeControlMessage(MoqtMessageType::kFetchCancel,
                                 WireVarInt62(message.request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeRequestsBlocked(
    const MoqtRequestsBlocked& message) {
  return SerializeControlMessage(MoqtMessageType::kRequestsBlocked,
                                 WireVarInt62(message.max_request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializePublish(const MoqtPublish& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kPublish)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  std::optional<uint64_t> group, object;
  if (message.largest_location.has_value()) {
    group = message.largest_location->group;
    object = message.largest_location->object;
  }
  return SerializeControlMessage(
      MoqtMessageType::kPublish, WireVarInt62(message.request_id),
      WireFullTrackName(message.full_track_name),
      WireVarInt62(message.track_alias), WireDeliveryOrder(message.group_order),
      WireBoolean(message.largest_location.has_value()),
      WireOptional<WireVarInt62>(group), WireOptional<WireVarInt62>(object),
      WireBoolean(message.forward), WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializePublishOk(
    const MoqtPublishOk& message) {
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kPublishOk)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  std::optional<uint64_t> start_group, start_object, end_group;
  switch (message.filter_type) {
    case MoqtFilterType::kNextGroupStart:
    case MoqtFilterType::kLatestObject:
      break;
    case MoqtFilterType::kAbsoluteStart:
    case MoqtFilterType::kAbsoluteRange:
      if (!message.start.has_value()) {
        QUICHE_BUG(QUICHE_BUG_invalid_filter_type)
            << "Serializing invalid MoQT filter type";
        return quiche::QuicheBuffer();
      }
      start_group = message.start->group;
      start_object = message.start->object;
      if (message.filter_type == MoqtFilterType::kAbsoluteStart) {
        break;
      }
      if (!message.end_group.has_value()) {
        QUICHE_BUG(QUICHE_BUG_invalid_filter_type)
            << "Serializing invalid MoQT filter type";
        return quiche::QuicheBuffer();
      }
      end_group = message.end_group;
      if (*end_group < *start_group) {
        QUICHE_BUG(QUICHE_BUG_invalid_filter_type)
            << "End group is less than start group";
        return quiche::QuicheBuffer();
      }
      break;
    default:
      QUICHE_BUG(QUICHE_BUG_invalid_filter_type)
          << "Serializing invalid MoQT filter type";
      return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(
      MoqtMessageType::kPublishOk, WireVarInt62(message.request_id),
      WireBoolean(message.forward), WireUint8(message.subscriber_priority),
      WireDeliveryOrder(message.group_order), WireVarInt62(message.filter_type),
      WireOptional<WireVarInt62>(start_group),
      WireOptional<WireVarInt62>(start_object),
      WireOptional<WireVarInt62>(end_group), WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializePublishError(
    const MoqtPublishError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kPublishError, WireVarInt62(message.request_id),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.error_reason));
}

quiche::QuicheBuffer MoqtFramer::SerializeObjectAck(
    const MoqtObjectAck& message) {
  return SerializeControlMessage(
      MoqtMessageType::kObjectAck, WireVarInt62(message.subscribe_id),
      WireVarInt62(message.group_id), WireVarInt62(message.object_id),
      WireVarInt62(SignedVarintSerializedForm(
          message.delta_from_deadline.ToMicroseconds())));
}

// static
bool MoqtFramer::ValidateObjectMetadata(const MoqtObject& object,
                                        bool is_datagram) {
  if (object.object_status != MoqtObjectStatus::kNormal &&
      object.payload_length > 0) {
    return false;
  }
  if (is_datagram && object.subgroup_id != object.object_id) {
    return false;
  }
  return true;
}

}  // namespace moqt
