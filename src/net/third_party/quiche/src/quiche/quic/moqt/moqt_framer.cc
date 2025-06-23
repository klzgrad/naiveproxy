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

class WireFullTrackName {
 public:
  using DataType = FullTrackName;

  // If |includes_name| is true, the last element in the tuple is the track
  // name and is therefore not counted in the prefix of the namespace tuple.
  WireFullTrackName(const FullTrackName& name, bool includes_name)
      : name_(name), includes_name_(includes_name) {}

  size_t GetLengthOnWire() {
    return quiche::ComputeLengthOnWire(
        WireVarInt62(num_elements()),
        WireSpan<WireStringWithVarInt62Length, std::string>(name_.tuple()));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    return quiche::SerializeIntoWriter(
        writer, WireVarInt62(num_elements()),
        WireSpan<WireStringWithVarInt62Length, std::string>(name_.tuple()));
  }

 private:
  size_t num_elements() const {
    return includes_name_ ? (name_.tuple().size() - 1) : name_.tuple().size();
  }

  const FullTrackName& name_;
  const bool includes_name_;
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
  if (!message.subgroup_id.has_value()) {
    QUICHE_BUG(QUICHE_BUG_serialize_object_header_02)
        << "Subgroup ID is not set on data stream";
    return quiche::QuicheBuffer();
  }
  if (!is_first_in_stream) {
    switch (message_type) {
      case MoqtDataStreamType::kStreamHeaderSubgroup:
        return (message.payload_length == 0)
                   ? Serialize(WireVarInt62(message.object_id),
                               WireStringWithVarInt62Length(
                                   message.extension_headers),
                               WireVarInt62(message.payload_length),
                               WireVarInt62(static_cast<uint64_t>(
                                   message.object_status)))
                   : Serialize(WireVarInt62(message.object_id),
                               WireStringWithVarInt62Length(
                                   message.extension_headers),
                               WireVarInt62(message.payload_length));
      case MoqtDataStreamType::kStreamHeaderFetch:
        return (message.payload_length == 0)
                   ? Serialize(WireVarInt62(message.group_id),
                               WireVarInt62(*message.subgroup_id),
                               WireVarInt62(message.object_id),
                               WireUint8(message.publisher_priority),
                               WireStringWithVarInt62Length(
                                   message.extension_headers),
                               WireVarInt62(message.payload_length),
                               WireVarInt62(static_cast<uint64_t>(
                                   message.object_status)))
                   : Serialize(WireVarInt62(message.group_id),
                               WireVarInt62(*message.subgroup_id),
                               WireVarInt62(message.object_id),
                               WireUint8(message.publisher_priority),
                               WireStringWithVarInt62Length(
                                   message.extension_headers),
                               WireVarInt62(message.payload_length));
      default:
        QUICHE_NOTREACHED();
        return quiche::QuicheBuffer();
    }
  }
  switch (message_type) {
    case MoqtDataStreamType::kStreamHeaderSubgroup:
      return (message.payload_length == 0)
                 ? Serialize(
                       WireVarInt62(message_type),
                       WireVarInt62(message.track_alias),
                       WireVarInt62(message.group_id),
                       WireVarInt62(*message.subgroup_id),
                       WireUint8(message.publisher_priority),
                       WireVarInt62(message.object_id),
                       WireStringWithVarInt62Length(message.extension_headers),
                       WireVarInt62(message.payload_length),
                       WireVarInt62(message.object_status))
                 : Serialize(
                       WireVarInt62(message_type),
                       WireVarInt62(message.track_alias),
                       WireVarInt62(message.group_id),
                       WireVarInt62(*message.subgroup_id),
                       WireUint8(message.publisher_priority),
                       WireVarInt62(message.object_id),
                       WireStringWithVarInt62Length(message.extension_headers),
                       WireVarInt62(message.payload_length));
    case MoqtDataStreamType::kStreamHeaderFetch:
      return (message.payload_length == 0)
                 ? Serialize(
                       WireVarInt62(message_type),
                       WireVarInt62(message.track_alias),
                       WireVarInt62(message.group_id),
                       WireVarInt62(*message.subgroup_id),
                       WireVarInt62(message.object_id),
                       WireUint8(message.publisher_priority),
                       WireStringWithVarInt62Length(message.extension_headers),
                       WireVarInt62(message.payload_length),
                       WireVarInt62(message.object_status))
                 : Serialize(
                       WireVarInt62(message_type),
                       WireVarInt62(message.track_alias),
                       WireVarInt62(message.group_id),
                       WireVarInt62(*message.subgroup_id),
                       WireVarInt62(message.object_id),
                       WireUint8(message.publisher_priority),
                       WireStringWithVarInt62Length(message.extension_headers),
                       WireVarInt62(message.payload_length));
    default:
      QUICHE_NOTREACHED();
      return quiche::QuicheBuffer();
  }
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
  if (message.object_status != MoqtObjectStatus::kNormal) {
    return Serialize(
        WireVarInt62(MoqtDatagramType::kObjectStatus),
        WireVarInt62(message.track_alias), WireVarInt62(message.group_id),
        WireVarInt62(message.object_id), WireUint8(message.publisher_priority),
        WireStringWithVarInt62Length(message.extension_headers),
        WireVarInt62(message.object_status));
  }
  return Serialize(
      WireVarInt62(MoqtDatagramType::kObject),
      WireVarInt62(message.track_alias), WireVarInt62(message.group_id),
      WireVarInt62(message.object_id), WireUint8(message.publisher_priority),
      WireStringWithVarInt62Length(message.extension_headers),
      WireVarInt62(message.payload_length), WireBytes(payload));
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
  switch (message.filter_type) {
    case MoqtFilterType::kNextGroupStart:
    case MoqtFilterType::kLatestObject:
      return SerializeControlMessage(
          MoqtMessageType::kSubscribe, WireVarInt62(message.request_id),
          WireVarInt62(message.track_alias),
          WireFullTrackName(message.full_track_name, true),
          WireUint8(message.subscriber_priority),
          WireDeliveryOrder(message.group_order), WireBoolean(message.forward),
          WireVarInt62(message.filter_type), WireKeyValuePairList(parameters));
    case MoqtFilterType::kAbsoluteStart:
      if (!message.start.has_value()) {
        return quiche::QuicheBuffer();
      };
      return SerializeControlMessage(
          MoqtMessageType::kSubscribe, WireVarInt62(message.request_id),
          WireVarInt62(message.track_alias),
          WireFullTrackName(message.full_track_name, true),
          WireUint8(message.subscriber_priority),
          WireDeliveryOrder(message.group_order), WireBoolean(message.forward),
          WireVarInt62(message.filter_type), WireVarInt62(message.start->group),
          WireVarInt62(message.start->object),
          WireKeyValuePairList(parameters));
    case MoqtFilterType::kAbsoluteRange:
      if (!message.start.has_value() || !message.end_group.has_value()) {
        return quiche::QuicheBuffer();
      }
      if (*message.end_group < message.start->group) {
        QUICHE_BUG(MoqtFramer_invalid_end_group) << "Invalid object range";
        return quiche::QuicheBuffer();
      }
      return SerializeControlMessage(
          MoqtMessageType::kSubscribe, WireVarInt62(message.request_id),
          WireVarInt62(message.track_alias),
          WireFullTrackName(message.full_track_name, true),
          WireUint8(message.subscriber_priority),
          WireDeliveryOrder(message.group_order), WireBoolean(message.forward),
          WireVarInt62(message.filter_type), WireVarInt62(message.start->group),
          WireVarInt62(message.start->object), WireVarInt62(*message.end_group),
          WireKeyValuePairList(parameters));
    default:
      QUICHE_BUG(MoqtFramer_end_group_missing) << "Subscribe framing error.";
      return quiche::QuicheBuffer();
  }
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
        WireVarInt62(message.expires.ToMilliseconds()),
        WireDeliveryOrder(message.group_order), WireUint8(1),
        WireVarInt62(message.largest_location->group),
        WireVarInt62(message.largest_location->object),
        WireKeyValuePairList(parameters));
  }
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeOk, WireVarInt62(message.request_id),
      WireVarInt62(message.expires.ToMilliseconds()),
      WireDeliveryOrder(message.group_order), WireUint8(0),
      WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeError(
    const MoqtSubscribeError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeError, WireVarInt62(message.request_id),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.reason_phrase),
      WireVarInt62(message.track_alias));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnsubscribe(
    const MoqtUnsubscribe& message) {
  return SerializeControlMessage(MoqtMessageType::kUnsubscribe,
                                 WireVarInt62(message.subscribe_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeDone(
    const MoqtSubscribeDone& message) {
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeDone, WireVarInt62(message.subscribe_id),
      WireVarInt62(message.status_code), WireVarInt62(message.stream_count),
      WireStringWithVarInt62Length(message.reason_phrase));
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
  return SerializeControlMessage(
      MoqtMessageType::kAnnounce,
      WireFullTrackName(message.track_namespace, false),
      WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceOk(
    const MoqtAnnounceOk& message) {
  return SerializeControlMessage(
      MoqtMessageType::kAnnounceOk,
      WireFullTrackName(message.track_namespace, false));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceError(
    const MoqtAnnounceError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kAnnounceError,
      WireFullTrackName(message.track_namespace, false),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.reason_phrase));
}

quiche::QuicheBuffer MoqtFramer::SerializeAnnounceCancel(
    const MoqtAnnounceCancel& message) {
  return SerializeControlMessage(
      MoqtMessageType::kAnnounceCancel,
      WireFullTrackName(message.track_namespace, false),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.reason_phrase));
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
  return SerializeControlMessage(
      MoqtMessageType::kTrackStatusRequest,
      WireFullTrackName(message.full_track_name, true),
      WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnannounce(
    const MoqtUnannounce& message) {
  return SerializeControlMessage(
      MoqtMessageType::kUnannounce,
      WireFullTrackName(message.track_namespace, false));
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
  return SerializeControlMessage(
      MoqtMessageType::kTrackStatus,
      WireFullTrackName(message.full_track_name, true),
      WireVarInt62(message.status_code), WireVarInt62(message.last_group),
      WireVarInt62(message.last_object), WireKeyValuePairList(parameters));
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
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeAnnounces,
      WireFullTrackName(message.track_namespace, false),
      WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeAnnouncesOk(
    const MoqtSubscribeAnnouncesOk& message) {
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeAnnouncesOk,
      WireFullTrackName(message.track_namespace, false));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeAnnouncesError(
    const MoqtSubscribeAnnouncesError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeAnnouncesError,
      WireFullTrackName(message.track_namespace, false),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.reason_phrase));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnsubscribeAnnounces(
    const MoqtUnsubscribeAnnounces& message) {
  return SerializeControlMessage(
      MoqtMessageType::kUnsubscribeAnnounces,
      WireFullTrackName(message.track_namespace, false));
}

quiche::QuicheBuffer MoqtFramer::SerializeMaxRequestId(
    const MoqtMaxRequestId& message) {
  return SerializeControlMessage(MoqtMessageType::kMaxRequestId,
                                 WireVarInt62(message.max_request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeFetch(const MoqtFetch& message) {
  if (!message.joining_fetch.has_value() &&
      (message.end_group < message.start_object.group ||
       (message.end_group == message.start_object.group &&
        message.end_object.has_value() &&
        *message.end_object < message.start_object.object))) {
    QUICHE_BUG(MoqtFramer_invalid_fetch) << "Invalid FETCH object range";
    return quiche::QuicheBuffer();
  }
  KeyValuePairList parameters;
  VersionSpecificParametersToKeyValuePairList(message.parameters, parameters);
  if (!ValidateVersionSpecificParameters(parameters, MoqtMessageType::kFetch)) {
    QUICHE_BUG(QUICHE_BUG_invalid_parameters)
        << "Serializing invalid MoQT parameters";
    return quiche::QuicheBuffer();
  }
  if (message.joining_fetch.has_value()) {
    return SerializeControlMessage(
        MoqtMessageType::kFetch, WireVarInt62(message.fetch_id),
        WireUint8(message.subscriber_priority),
        WireDeliveryOrder(message.group_order),
        WireVarInt62(FetchType::kJoining),
        WireVarInt62(message.joining_fetch->joining_subscribe_id),
        WireVarInt62(message.joining_fetch->preceding_group_offset),
        WireKeyValuePairList(parameters));
  }
  return SerializeControlMessage(
      MoqtMessageType::kFetch, WireVarInt62(message.fetch_id),
      WireUint8(message.subscriber_priority),
      WireDeliveryOrder(message.group_order),
      WireVarInt62(FetchType::kStandalone),
      WireFullTrackName(message.full_track_name, true),
      WireVarInt62(message.start_object.group),
      WireVarInt62(message.start_object.object),
      WireVarInt62(message.end_group),
      WireVarInt62(message.end_object.has_value() ? *message.end_object + 1
                                                  : 0),
      WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeFetchCancel(
    const MoqtFetchCancel& message) {
  return SerializeControlMessage(MoqtMessageType::kFetchCancel,
                                 WireVarInt62(message.subscribe_id));
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
  return SerializeControlMessage(MoqtMessageType::kFetchOk,
                                 WireVarInt62(message.subscribe_id),
                                 WireDeliveryOrder(message.group_order),
                                 WireVarInt62(message.largest_id.group),
                                 WireVarInt62(message.largest_id.object),
                                 WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeFetchError(
    const MoqtFetchError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kFetchError, WireVarInt62(message.subscribe_id),
      WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.reason_phrase));
}

quiche::QuicheBuffer MoqtFramer::SerializeRequestsBlocked(
    const MoqtRequestsBlocked& message) {
  return SerializeControlMessage(MoqtMessageType::kRequestsBlocked,
                                 WireVarInt62(message.max_request_id));
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
  if (is_datagram == object.subgroup_id.has_value()) {
    return false;
  }
  return true;
}

}  // namespace moqt
