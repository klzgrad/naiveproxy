// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_framer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "absl/container/fixed_array.h"
#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_types.h"
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
  explicit WireKeyValuePairList(const KeyValuePairList& list,
                                bool length_prefix = true)
      : list_(list), length_prefix_(length_prefix) {}

  size_t GetLengthOnWire() {
    size_t total =
        length_prefix_ ? WireVarInt62(list_.size()).GetLengthOnWire() : 0;
    uint64_t last_key = 0;
    list_.ForEach([&](uint64_t key,
                      std::variant<uint64_t, absl::string_view> value) {
      total += std::visit(
          absl::Overload{
              [&](uint64_t val) {
                return WireKeyVarIntPair(key - last_key, val).GetLengthOnWire();
              },
              [&](absl::string_view val) {
                return WireKeyStringPair(key - last_key, val).GetLengthOnWire();
              }},
          value);
      last_key = key;
      return true;
    });
    return total;
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    if (length_prefix_) {
      WireVarInt62(list_.size()).SerializeIntoWriter(writer);
    }
    uint64_t last_key = 0;
    list_.ForEach(
        [&](uint64_t key, std::variant<uint64_t, absl::string_view> value) {
          absl::Status status = std::visit(
              absl::Overload{[&](uint64_t val) {
                               return WireKeyVarIntPair(key - last_key, val)
                                   .SerializeIntoWriter(writer);
                             },
                             [&](absl::string_view val) {
                               return WireKeyStringPair(key - last_key, val)
                                   .SerializeIntoWriter(writer);
                             }},
              value);
          last_key = key;
          return quiche::IsWriterStatusOk(status);
        });
    return absl::OkStatus();
  }

 private:
  const KeyValuePairList& list_;
  const bool length_prefix_;
};

class WireTrackNamespace {
 public:
  WireTrackNamespace(const TrackNamespace& name) : namespace_(name) {}

  size_t GetLengthOnWire() {
    absl::FixedArray<absl::string_view> tuple(namespace_.tuple().begin(),
                                              namespace_.tuple().end());
    return quiche::ComputeLengthOnWire(
        WireVarInt62(namespace_.number_of_elements()),
        WireSpan<WireStringWithVarInt62Length, absl::string_view>(
            absl::MakeSpan(tuple)));
  }
  absl::Status SerializeIntoWriter(quiche::QuicheDataWriter& writer) {
    absl::FixedArray<absl::string_view> tuple(namespace_.tuple().begin(),
                                              namespace_.tuple().end());
    return quiche::SerializeIntoWriter(
        writer, WireVarInt62(namespace_.number_of_elements()),
        WireSpan<WireStringWithVarInt62Length, absl::string_view>(
            absl::MakeSpan(tuple)));
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

[[maybe_unused]] WireUint8 WireDeliveryOrder(
    std::optional<MoqtDeliveryOrder> delivery_order) {
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

quiche::QuicheBuffer SerializeAuthToken(const AuthToken& token) {
  return Serialize(WireVarInt62(token.alias_type),
                   WireOptional<WireVarInt62>(token.alias),
                   WireOptional<WireVarInt62>(token.type),
                   WireOptional<WireBytes>(token.value));
}

quiche::QuicheBuffer SerializeSubscriptionFilter(
    const SubscriptionFilter& filter) {
  switch (filter.type()) {
    case MoqtFilterType::kNextGroupStart:
      return Serialize(WireVarInt62(filter.type()));
    case MoqtFilterType::kLargestObject:
      return Serialize(WireVarInt62(filter.type()));
    case MoqtFilterType::kAbsoluteStart:
      return Serialize(
          WireVarInt62(filter.type()),
          WireKeyVarIntPair(filter.start().group, filter.start().object));
    case MoqtFilterType::kAbsoluteRange:
      return Serialize(
          WireVarInt62(filter.type()),
          WireKeyVarIntPair(filter.start().group, filter.start().object),
          WireVarInt62(filter.end_group()));
  }
}

quiche::QuicheBuffer SerializeLocation(const Location& location) {
  return Serialize(WireKeyVarIntPair(location.group, location.object));
}

}  // namespace

KeyValuePairList SetupParameters::ToKeyValuePairList() const {
  KeyValuePairList out;
  if (max_request_id.has_value()) {
    out.insert(static_cast<uint64_t>(SetupParameter::kMaxRequestId),
               *max_request_id);
  }
  if (max_auth_token_cache_size.has_value()) {
    out.insert(static_cast<uint64_t>(SetupParameter::kMaxAuthTokenCacheSize),
               *max_auth_token_cache_size);
  }
  if (path.has_value()) {
    out.insert(static_cast<uint64_t>(SetupParameter::kPath), *path);
  }
  for (const AuthToken& token : authorization_tokens) {
    out.insert(static_cast<uint64_t>(SetupParameter::kAuthorizationToken),
               SerializeAuthToken(token).AsStringView());
  }
  if (authority.has_value()) {
    out.insert(static_cast<uint64_t>(SetupParameter::kAuthority), *authority);
  }
  if (moqt_implementation.has_value()) {
    out.insert(static_cast<uint64_t>(SetupParameter::kMoqtImplementation),
               *moqt_implementation);
  }
  if (support_object_acks.has_value()) {
    out.insert(static_cast<uint64_t>(SetupParameter::kSupportObjectAcks),
               *support_object_acks ? 1ULL : 0ULL);
  }
  return out;
}

KeyValuePairList MessageParameters::ToKeyValuePairList() const {
  KeyValuePairList list;
  if (delivery_timeout.has_value()) {
    // Value cannot be zero.
    int64_t milliseconds =
        std::max(delivery_timeout->ToMilliseconds(), int64_t{1});
    list.insert(static_cast<uint64_t>(MessageParameter::kDeliveryTimeout),
                static_cast<uint64_t>(milliseconds));
  }
  for (const AuthToken& token : authorization_tokens) {
    list.insert(static_cast<uint64_t>(MessageParameter::kAuthorizationToken),
                SerializeAuthToken(token).AsStringView());
  }
  if (expires.has_value()) {
    list.insert(static_cast<uint64_t>(MessageParameter::kExpires),
                static_cast<uint64_t>(expires->ToMilliseconds()));
  }
  if (largest_object.has_value()) {
    list.insert(static_cast<uint64_t>(MessageParameter::kLargestObject),
                SerializeLocation(*largest_object).AsStringView());
  }
  if (forward_has_value()) {
    list.insert(static_cast<uint64_t>(MessageParameter::kForward),
                forward() ? 1ULL : 0ULL);
  }
  if (subscriber_priority.has_value()) {
    list.insert(static_cast<uint64_t>(MessageParameter::kSubscriberPriority),
                *subscriber_priority);
  }
  if (subscription_filter.has_value()) {
    list.insert(
        static_cast<uint64_t>(MessageParameter::kSubscriptionFilter),
        SerializeSubscriptionFilter(*subscription_filter).AsStringView());
  }
  if (group_order.has_value()) {
    list.insert(static_cast<uint64_t>(MessageParameter::kGroupOrder),
                static_cast<uint64_t>(*group_order));
  }
  if (new_group_request.has_value()) {
    list.insert(static_cast<uint64_t>(MessageParameter::kNewGroupRequest),
                *new_group_request);
  }
  if (oack_window_size.has_value()) {
    list.insert(static_cast<uint64_t>(MessageParameter::kOackWindowSize),
                static_cast<uint64_t>(oack_window_size->ToMicroseconds()));
  }
  return list;
}

quiche::QuicheBuffer MoqtFramer::SerializeObjectHeader(
    const MoqtObject& message, MoqtDataStreamType message_type,
    std::optional<PublishedObjectMetadata>& previous_object_in_stream) {
  if (!ValidateObjectMetadata(message)) {
    QUICHE_BUG(QUICHE_BUG_serialize_object_header_01)
        << "Object metadata is invalid";
    return quiche::QuicheBuffer();
  }
  // Many fields are optional because the stream type or Fetch serialization
  // omits them.
  std::optional<uint64_t> stream_type;
  std::optional<uint64_t> track_id;  // Track alias or FETCH ID.
  std::optional<uint64_t> group_id;
  std::optional<uint64_t> subgroup_id;
  std::optional<uint64_t> object_id;
  std::optional<uint8_t> publisher_priority;
  std::optional<absl::string_view> extension_headers;
  uint64_t payload_length = message.payload_length;
  bool is_first_in_stream = !previous_object_in_stream.has_value();
  if (is_first_in_stream) {
    stream_type = message_type.value();
    track_id = message.track_alias;
  }
  if (message_type.IsFetch()) {
    MoqtFetchSerialization serialization;
    if (is_first_in_stream) {
      serialization = MoqtFetchSerialization(message);
    } else {
      serialization =
          MoqtFetchSerialization(message, *previous_object_in_stream);
    }
    if (serialization.has_group_id()) {
      group_id = message.group_id;
    }
    if (serialization.has_subgroup_id()) {
      subgroup_id = message.subgroup_id;
    }
    if (serialization.has_object_id()) {
      object_id = message.object_id;
    }
    if (serialization.has_priority()) {
      publisher_priority = message.publisher_priority;
    }
    if (serialization.has_extensions()) {
      extension_headers = message.extension_headers;
    }
    return Serialize(
        WireOptional<WireVarInt62>(stream_type),
        WireOptional<WireVarInt62>(track_id),
        WireVarInt62(serialization.value()),
        WireOptional<WireVarInt62>(group_id),
        WireOptional<WireVarInt62>(subgroup_id),
        WireOptional<WireVarInt62>(object_id),
        WireOptional<WireUint8>(publisher_priority),
        WireOptional<WireStringWithVarInt62Length>(extension_headers),
        WireVarInt62(payload_length));
  }
  // Subgroup stream.
  if (!message.subgroup_id.has_value()) {
    QUICHE_BUG(QUICHE_BUG_serialize_object_header_02)
        << "Subgroup ID is missing";
    return quiche::QuicheBuffer();
  }
  if (is_first_in_stream) {
    group_id = message.group_id;
    if (message_type.IsSubgroupPresent()) {
      subgroup_id = message.subgroup_id;
    }
    if (!message_type.HasDefaultPriority()) {
      publisher_priority = message.publisher_priority;
    }
  }
  object_id = message.object_id;
  if (!is_first_in_stream) {
    *object_id -= (previous_object_in_stream->location.object + 1);
  }
  if (message_type.AreExtensionHeadersPresent()) {
    extension_headers = message.extension_headers;
  }
  std::optional<uint64_t> object_status;
  if (payload_length == 0) {
    object_status = static_cast<uint64_t>(message.object_status);
  }
  return Serialize(
      WireOptional<WireVarInt62>(stream_type),
      WireOptional<WireVarInt62>(track_id),
      WireOptional<WireVarInt62>(group_id),
      WireOptional<WireVarInt62>(subgroup_id),
      WireOptional<WireUint8>(publisher_priority), WireVarInt62(*object_id),
      WireOptional<WireStringWithVarInt62Length>(extension_headers),
      WireVarInt62(message.payload_length),
      WireOptional<WireVarInt62>(object_status));
}

quiche::QuicheBuffer MoqtFramer::SerializeObjectDatagram(
    const MoqtObject& message, absl::string_view payload,
    MoqtPriority default_priority) {
  if (!ValidateObjectMetadata(message) || message.subgroup_id.has_value()) {
    QUICHE_BUG(QUICHE_BUG_serialize_object_datagram_01)
        << "Object metadata is invalid";
    return quiche::QuicheBuffer();
  }
  if (message.payload_length != payload.length()) {
    QUICHE_BUG(QUICHE_BUG_serialize_object_datagram_03)
        << "Payload length does not match payload";
    return quiche::QuicheBuffer();
  }
  MoqtDatagramType datagram_type(
      !payload.empty(), !message.extension_headers.empty(),
      message.object_status == MoqtObjectStatus::kEndOfGroup,
      message.publisher_priority == default_priority, message.object_id == 0);
  std::optional<uint64_t> object_id =
      datagram_type.has_object_id() ? std::optional<uint64_t>(message.object_id)
                                    : std::nullopt;
  std::optional<uint8_t> publisher_priority =
      datagram_type.has_default_priority()
          ? std::nullopt
          : std::optional<uint8_t>(message.publisher_priority);
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
      WireVarInt62(message.group_id), WireOptional<WireVarInt62>(object_id),
      WireOptional<WireUint8>(publisher_priority),
      WireOptional<WireStringWithVarInt62Length>(extensions),
      WireOptional<WireVarInt62>(object_status),
      WireOptional<WireBytes>(raw_payload));
}

quiche::QuicheBuffer MoqtFramer::SerializeClientSetup(
    const MoqtClientSetup& message) {
  KeyValuePairList parameters;
  if (!FillAndValidateSetupParameters(MoqtMessageType::kClientSetup,
                                      message.parameters, parameters)) {
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(MoqtMessageType::kClientSetup,
                                 WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeServerSetup(
    const MoqtServerSetup& message) {
  KeyValuePairList parameters;
  if (!FillAndValidateSetupParameters(MoqtMessageType::kServerSetup,
                                      message.parameters, parameters)) {
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(MoqtMessageType::kServerSetup,
                                 WireKeyValuePairList(parameters));
}

quiche::QuicheBuffer MoqtFramer::SerializeRequestOk(
    const MoqtRequestOk& message) {
  return SerializeControlMessage(
      MoqtMessageType::kRequestOk, WireVarInt62(message.request_id),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribe(
    const MoqtSubscribe& message, MoqtMessageType message_type) {
  return SerializeControlMessage(
      message_type, WireVarInt62(message.request_id),
      WireFullTrackName(message.full_track_name),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeOk(
    const MoqtSubscribeOk& message, MoqtMessageType message_type) {
  if (!message.extensions.Validate()) {
    QUICHE_BUG(QUICHE_BUG_serialize_subscribe_ok_01)
        << "Subscribe OK extensions are ill-formed";
    return quiche::QuicheBuffer();
  }
  return SerializeControlMessage(
      message_type, WireVarInt62(message.request_id),
      WireVarInt62(message.track_alias),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()),
      WireKeyValuePairList(message.extensions, false));
}

quiche::QuicheBuffer MoqtFramer::SerializeRequestError(
    const MoqtRequestError& message) {
  return SerializeControlMessage(
      MoqtMessageType::kRequestError, WireVarInt62(message.request_id),
      WireVarInt62(message.error_code),
      WireVarInt62(message.retry_interval.has_value()
                       ? message.retry_interval->ToMilliseconds() + 1
                       : 0),
      WireStringWithVarInt62Length(message.reason_phrase));
}

quiche::QuicheBuffer MoqtFramer::SerializeUnsubscribe(
    const MoqtUnsubscribe& message) {
  return SerializeControlMessage(MoqtMessageType::kUnsubscribe,
                                 WireVarInt62(message.request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializePublishDone(
    const MoqtPublishDone& message) {
  return SerializeControlMessage(
      MoqtMessageType::kPublishDone, WireVarInt62(message.request_id),
      WireVarInt62(message.status_code), WireVarInt62(message.stream_count),
      WireStringWithVarInt62Length(message.error_reason));
}

quiche::QuicheBuffer MoqtFramer::SerializeRequestUpdate(
    const MoqtRequestUpdate& message) {
  return SerializeControlMessage(
      MoqtMessageType::kRequestUpdate, WireVarInt62(message.request_id),
      WireVarInt62(message.existing_request_id),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()));
}

quiche::QuicheBuffer MoqtFramer::SerializePublishNamespace(
    const MoqtPublishNamespace& message) {
  return SerializeControlMessage(
      MoqtMessageType::kPublishNamespace, WireVarInt62(message.request_id),
      WireTrackNamespace(message.track_namespace),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()));
}

quiche::QuicheBuffer MoqtFramer::SerializePublishNamespaceDone(
    const MoqtPublishNamespaceDone& message) {
  return SerializeControlMessage(MoqtMessageType::kPublishNamespaceDone,
                                 WireVarInt62(message.request_id));
}

quiche::QuicheBuffer MoqtFramer::SerializeNamespace(
    const MoqtNamespace& message) {
  return SerializeControlMessage(
      MoqtMessageType::kNamespace,
      WireTrackNamespace(message.track_namespace_suffix));
}

quiche::QuicheBuffer MoqtFramer::SerializeNamespaceDone(
    const MoqtNamespaceDone& message) {
  return SerializeControlMessage(
      MoqtMessageType::kNamespaceDone,
      WireTrackNamespace(message.track_namespace_suffix));
}

quiche::QuicheBuffer MoqtFramer::SerializePublishNamespaceCancel(
    const MoqtPublishNamespaceCancel& message) {
  return SerializeControlMessage(
      MoqtMessageType::kPublishNamespaceCancel,
      WireVarInt62(message.request_id), WireVarInt62(message.error_code),
      WireStringWithVarInt62Length(message.error_reason));
}

quiche::QuicheBuffer MoqtFramer::SerializeTrackStatus(
    const MoqtTrackStatus& message) {
  return SerializeSubscribe(message, MoqtMessageType::kTrackStatus);
}

quiche::QuicheBuffer MoqtFramer::SerializeGoAway(const MoqtGoAway& message) {
  return SerializeControlMessage(
      MoqtMessageType::kGoAway,
      WireStringWithVarInt62Length(message.new_session_uri));
}

quiche::QuicheBuffer MoqtFramer::SerializeSubscribeNamespace(
    const MoqtSubscribeNamespace& message) {
  return SerializeControlMessage(
      MoqtMessageType::kSubscribeNamespace, WireVarInt62(message.request_id),
      WireTrackNamespace(message.track_namespace_prefix),
      WireVarInt62(message.subscribe_options),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()));
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
    if (standalone_fetch.end_location < standalone_fetch.start_location) {
      QUICHE_BUG(MoqtFramer_invalid_fetch) << "Invalid FETCH object range";
      return quiche::QuicheBuffer();
    }
  }
  if (std::holds_alternative<StandaloneFetch>(message.fetch)) {
    const StandaloneFetch& standalone_fetch =
        std::get<StandaloneFetch>(message.fetch);
    return SerializeControlMessage(
        MoqtMessageType::kFetch, WireVarInt62(message.request_id),
        WireVarInt62(FetchType::kStandalone),
        WireFullTrackName(standalone_fetch.full_track_name),
        WireVarInt62(standalone_fetch.start_location.group),
        WireVarInt62(standalone_fetch.start_location.object),
        WireVarInt62(standalone_fetch.end_location.group),
        WireVarInt62(standalone_fetch.end_location.object == kMaxObjectId
                         ? 0
                         : standalone_fetch.end_location.object + 1),
        WireKeyValuePairList(message.parameters.ToKeyValuePairList()));
  }
  uint64_t request_id, joining_start;
  if (std::holds_alternative<JoiningFetchRelative>(message.fetch)) {
    const JoiningFetchRelative& joining_fetch =
        std::get<JoiningFetchRelative>(message.fetch);
    request_id = joining_fetch.joining_request_id;
    joining_start = joining_fetch.joining_start;
  } else {
    const JoiningFetchAbsolute& joining_fetch =
        std::get<JoiningFetchAbsolute>(message.fetch);
    request_id = joining_fetch.joining_request_id;
    joining_start = joining_fetch.joining_start;
  }
  return SerializeControlMessage(
      MoqtMessageType::kFetch, WireVarInt62(message.request_id),
      WireVarInt62(message.fetch.index() + 1), WireVarInt62(request_id),
      WireVarInt62(joining_start),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()));
}

quiche::QuicheBuffer MoqtFramer::SerializeFetchOk(const MoqtFetchOk& message) {
  return SerializeControlMessage(
      MoqtMessageType::kFetchOk, WireVarInt62(message.request_id),
      WireBoolean(message.end_of_track),
      WireVarInt62(message.end_location.group),
      WireVarInt62(message.end_location.object == kMaxObjectId
                       ? 0
                       : (message.end_location.object + 1)),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()),
      WireKeyValuePairList(message.extensions, false));
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
  return SerializeControlMessage(
      MoqtMessageType::kPublish, WireVarInt62(message.request_id),
      WireFullTrackName(message.full_track_name),
      WireVarInt62(message.track_alias),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()),
      WireKeyValuePairList(message.extensions, false));
}

quiche::QuicheBuffer MoqtFramer::SerializePublishOk(
    const MoqtPublishOk& message) {
  return SerializeControlMessage(
      MoqtMessageType::kPublishOk, WireVarInt62(message.request_id),
      WireKeyValuePairList(message.parameters.ToKeyValuePairList()));
}

quiche::QuicheBuffer MoqtFramer::SerializeObjectAck(
    const MoqtObjectAck& message) {
  return SerializeControlMessage(
      MoqtMessageType::kObjectAck, WireVarInt62(message.subscribe_id),
      WireVarInt62(message.group_id), WireVarInt62(message.object_id),
      WireVarInt62(SignedVarintSerializedForm(
          message.delta_from_deadline.ToMicroseconds())));
}

bool MoqtFramer::FillAndValidateSetupParameters(
    MoqtMessageType message_type, const SetupParameters& parameters,
    KeyValuePairList& out) {
  if (SetupParametersAllowedByMessage(parameters, message_type,
                                      using_webtrans_) != MoqtError::kNoError) {
    QUICHE_BUG(QUICHE_BUG_invalid_setup_parameters)
        << "Invalid setup parameters for "
        << MoqtMessageTypeToString(message_type);
    return false;
  }
  out = parameters.ToKeyValuePairList();
  return true;
}

// static
bool MoqtFramer::ValidateObjectMetadata(const MoqtObject& object) {
  return (object.object_status == MoqtObjectStatus::kNormal ||
          object.object_status == MoqtObjectStatus::kEndOfGroup ||
          object.payload_length == 0);
}

}  // namespace moqt
