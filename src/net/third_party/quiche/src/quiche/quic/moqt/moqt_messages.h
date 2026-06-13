// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Structured data for message types in draft-ietf-moq-transport-02.

#ifndef QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
#define QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <variant>

#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace moqt {

inline constexpr quic::ParsedQuicVersionVector GetMoqtSupportedQuicVersions() {
  return quic::ParsedQuicVersionVector{quic::ParsedQuicVersion::RFCv1()};
}

// The maximum length of a message, excluding any OBJECT payload. This prevents
// DoS attack via forcing the parser to buffer a large message (OBJECT payloads
// are not buffered by the parser).
inline constexpr size_t kMaxMessageHeaderSize = 2048;

class QUICHE_EXPORT MoqtDataStreamType {
 public:
  static constexpr uint64_t kFetch = 0x05;
  static constexpr uint64_t kPadding = 0x26d3;
  static constexpr uint64_t kSubgroup = 0x10;
  static constexpr uint64_t kExtensions = 0x01;
  static constexpr uint64_t kEndOfGroup = 0x08;
  static constexpr uint64_t kDefaultPriority = 0x20;
  // These two cannot simultaneously be true;
  static constexpr uint64_t kFirstObjectId = 0x02;
  static constexpr uint64_t kSubgroupId = 0x04;

  MoqtDataStreamType() : value_(0) {}
  // Factory functions.
  static std::optional<MoqtDataStreamType> FromValue(uint64_t value) {
    MoqtDataStreamType stream_type(value);
    if (stream_type.IsFetch() || stream_type.IsPadding()) {
      return stream_type;
    }
    if (!(value & kSubgroup)) {
      return std::nullopt;
    }
    if (value > (kSubgroup | kExtensions | kEndOfGroup | kDefaultPriority |
                 kFirstObjectId | kSubgroupId)) {
      // Reserved bits.
      return std::nullopt;
    }
    if ((value & kSubgroupId) && (value & kFirstObjectId)) {
      return std::nullopt;
    }
    return stream_type;
  }
  static MoqtDataStreamType Fetch() { return MoqtDataStreamType(kFetch); }
  static MoqtDataStreamType Padding() { return MoqtDataStreamType(kPadding); }
  static MoqtDataStreamType Subgroup(uint64_t subgroup_id,
                                     uint64_t first_object_id,
                                     bool no_extension_headers,
                                     bool default_priority,
                                     bool end_of_group = false) {
    uint64_t value = kSubgroup;
    if (!no_extension_headers) {
      value |= kExtensions;
    }
    if (end_of_group) {
      value |= kEndOfGroup;
    }
    if (default_priority) {
      value |= kDefaultPriority;
    }
    if (subgroup_id == 0) {
      return MoqtDataStreamType(value);
    }
    if (subgroup_id == first_object_id) {
      value |= kFirstObjectId;
    } else {
      value |= kSubgroupId;
    }
    return MoqtDataStreamType(value);
  }
  MoqtDataStreamType(const MoqtDataStreamType& other) = default;
  bool IsFetch() const { return value_ == kFetch; }
  bool IsPadding() const { return value_ == kPadding; }
  bool IsSubgroup() const { return value_ & kSubgroup; }
  bool IsSubgroupPresent() const {
    return IsSubgroup() && (value_ & kSubgroupId);
  }
  bool SubgroupIsZero() const {
    return IsSubgroup() && !(value_ & (kSubgroupId | kFirstObjectId));
  }
  bool SubgroupIsFirstObjectId() const {
    return IsSubgroup() && (value_ & kFirstObjectId);
  }
  bool AreExtensionHeadersPresent() const {
    return IsSubgroup() && (value_ & kExtensions);
  }
  bool EndOfGroupInStream() const {
    return IsSubgroup() && (value_ & kEndOfGroup);
  }
  bool HasDefaultPriority() const {
    return IsSubgroup() && (value_ & kDefaultPriority);
  }

  uint64_t value() const { return value_; }
  MoqtDataStreamType& operator=(const MoqtDataStreamType& other) = default;
  bool operator==(const MoqtDataStreamType& other) const = default;

 private:
  explicit MoqtDataStreamType(uint64_t value) : value_(value) {}
  uint64_t value_;
};

class QUICHE_EXPORT MoqtDatagramType {
 public:
  static constexpr uint64_t kExtensions = 0x01;
  static constexpr uint64_t kEndOfGroup = 0x02;
  static constexpr uint64_t kZeroObjectId = 0x04;
  static constexpr uint64_t kDefaultPriority = 0x08;
  static constexpr uint64_t kStatus = 0x20;
  // The arguments here are properties of the object. The constructor creates
  // the appropriate type given those properties and the spec restrictions.
  MoqtDatagramType(bool payload, bool extension, bool end_of_group,
                   bool default_priority, bool zero_object_id)
      : value_(0) {
    // Avoid illegal types. Status cannot coexist with the zero-object-id flag
    // or the end-of-group flag.
    if (!payload && !end_of_group) {
      // The only way to express non-normal, non-end-of-group with no payload is
      // with an explicit status, so we cannot utilize object ID compression.
      zero_object_id = false;
    } else if (zero_object_id) {
      // zero-object-id saves a byte; no-payload does not.
      payload = true;
    } else if (!payload) {
      // If it's an empty end-of-group object, use the explict status because
      // it's more readable.
      end_of_group = false;
    }
    if (extension) {
      value_ |= kExtensions;
    }
    if (end_of_group) {
      value_ |= kEndOfGroup;
    }
    if (zero_object_id) {
      value_ |= kZeroObjectId;
    }
    if (default_priority) {
      value_ |= kDefaultPriority;
    }
    if (!payload) {
      value_ |= kStatus;
    }
  }
  static std::optional<MoqtDatagramType> FromValue(uint64_t value) {
    if (value > (kExtensions | kEndOfGroup | kZeroObjectId | kDefaultPriority |
                 kStatus)) {
      return std::nullopt;
    }
    if ((value & kStatus) && (value & kEndOfGroup)) {
      return std::nullopt;
    }
    return MoqtDatagramType(value);
  }
  bool has_status() const { return value_ & kStatus; }
  bool has_default_priority() const { return value_ & kDefaultPriority; }
  bool has_object_id() const { return !(value_ & kZeroObjectId); }
  bool end_of_group() const { return value_ & kEndOfGroup; }
  bool has_extension() const { return value_ & kExtensions; }
  uint64_t value() const { return value_; }

  bool operator==(const MoqtDatagramType& other) const = default;

 private:
  uint64_t value_;
  explicit MoqtDatagramType(uint64_t value) : value_(value) {}
};

enum class QUICHE_EXPORT MoqtMessageType : uint64_t {
  kRequestUpdate = 0x02,
  kSubscribe = 0x03,
  kSubscribeOk = 0x04,
  kRequestError = 0x05,
  kPublishNamespace = 0x06,
  kRequestOk = 0x07,
  kNamespace = 0x08,
  kPublishNamespaceDone = 0x09,
  kUnsubscribe = 0x0a,
  kPublishDone = 0x0b,
  kPublishNamespaceCancel = 0x0c,
  kTrackStatus = 0x0d,
  kNamespaceDone = 0x0e,
  kGoAway = 0x10,
  kSubscribeNamespace = 0x11,
  kMaxRequestId = 0x15,
  kFetch = 0x16,
  kFetchCancel = 0x17,
  kFetchOk = 0x18,
  kRequestsBlocked = 0x1a,
  kPublish = 0x1d,
  kPublishOk = 0x1e,
  kClientSetup = 0x20,
  kServerSetup = 0x21,

  // QUICHE-specific extensions.

  // kObjectAck (OACK for short) is a frame used by the receiver indicating that
  // it has received and processed the specified object.
  kObjectAck = 0x3184,
};

struct SubgroupPriority {
  uint8_t publisher_priority = 0xf0;
  uint64_t subgroup_id = 0;

  auto operator<=>(const SubgroupPriority&) const = default;
};

// TODO(martinduke): Collapse both Setup messages into SetupParameters.
struct QUICHE_EXPORT MoqtClientSetup {
  SetupParameters parameters;
};

struct QUICHE_EXPORT MoqtServerSetup {
  SetupParameters parameters;
};

// These codes do not appear on the wire.
enum class QUICHE_EXPORT MoqtForwardingPreference : uint8_t {
  kSubgroup,
  kDatagram,
};

MoqtObjectStatus IntegerToObjectStatus(uint64_t integer);

// The data contained in every Object message, although the message type
// implies some of the values.
struct QUICHE_EXPORT MoqtObject {
  uint64_t track_alias;  // For FETCH, this is the subscribe ID.
  uint64_t group_id;
  uint64_t object_id;
  MoqtPriority publisher_priority;
  std::string extension_headers;  // Raw, unparsed extension headers.
  MoqtObjectStatus object_status;
  std::optional<uint64_t> subgroup_id;  // Only for subgroup objects.
  uint64_t payload_length;
};

class QUICHE_EXPORT MoqtFetchSerialization {
 public:
  static constexpr uint64_t kSubgroupIdMask = 0x03;
  static constexpr uint64_t kSubgroupIdZero = 0x00;
  static constexpr uint64_t kPriorSubgroupId = 0x01;
  static constexpr uint64_t kPriorSubgroupIdPlusOne = 0x02;
  static constexpr uint64_t kHasSubgroupId = 0x03;
  static constexpr uint64_t kHasObjectId = 0x04;
  static constexpr uint64_t kHasGroupId = 0x08;
  static constexpr uint64_t kHasPriority = 0x10;
  static constexpr uint64_t kHasExtensions = 0x20;
  static constexpr uint64_t kIsDatagram = 0x40;

  static constexpr uint64_t kMaxFetchSerialization =
      kIsDatagram | kHasExtensions | kHasPriority | kHasGroupId | kHasObjectId |
      kSubgroupIdMask;

  static constexpr uint64_t kEndOfNonExistentRange = 0x8c;
  static constexpr uint64_t kEndOfUnknownRange = 0x10c;

  MoqtFetchSerialization() = default;
  // Serialization for the first object in a stream.
  MoqtFetchSerialization(const MoqtObject& object) {
    if (!object.subgroup_id.has_value()) {
      value_ |= kIsDatagram;
    } else {
      if (*object.subgroup_id == 0) {
        value_ |= kSubgroupIdZero;
      } else {
        value_ |= kHasSubgroupId;
      }
    }
    value_ |= (kHasGroupId | kHasObjectId | kHasPriority);
    if (!object.extension_headers.empty()) {
      value_ |= kHasExtensions;
    }
  }
  // Serialization for a subsequent object in a stream.
  MoqtFetchSerialization(const MoqtObject& object,
                         const PublishedObjectMetadata& previous_object) {
    uint64_t value = 0;
    if (!object.subgroup_id.has_value()) {
      value |= kIsDatagram;
    } else if (object.subgroup_id == 0) {
      value |= kSubgroupIdZero;
    } else if (!previous_object.subgroup.has_value()) {
      value |= kHasSubgroupId;  // Can't use previous value.
    } else if (object.subgroup_id == previous_object.subgroup) {
      value |= kPriorSubgroupId;
    } else if (*object.subgroup_id == *previous_object.subgroup + 1) {
      value |= kPriorSubgroupIdPlusOne;
    } else {
      value |= kHasSubgroupId;
    }
    if (object.object_id != previous_object.location.object + 1) {
      value |= kHasObjectId;
    }
    if (object.group_id != previous_object.location.group) {
      value |= kHasGroupId;
    }
    if (object.publisher_priority != previous_object.publisher_priority) {
      value |= kHasPriority;
    }
    if (!object.extension_headers.empty()) {
      value |= kHasExtensions;
    }
    value_ = value;
  }
  static std::optional<MoqtFetchSerialization> FromValue(uint64_t value) {
    if (value > kMaxFetchSerialization && value != kEndOfUnknownRange &&
        value != kEndOfNonExistentRange) {
      return std::nullopt;
    }
    return MoqtFetchSerialization(value);
  }
  bool has_subgroup_id() const {
    return ((value_ & kSubgroupIdMask) == kHasSubgroupId) && !is_datagram();
  }
  bool zero_subgroup_id() const {
    return (value_ & kSubgroupIdMask) == kSubgroupIdZero && !is_datagram();
  }
  bool prior_subgroup_id() const {
    return (value_ & kSubgroupIdMask) == kPriorSubgroupId && !is_datagram();
  }
  bool prior_subgroup_id_plus_one() const {
    return (value_ & kSubgroupIdMask) == kPriorSubgroupIdPlusOne &&
           !is_datagram();
  }
  bool has_object_id() const { return value_ & kHasObjectId; }
  bool has_group_id() const { return value_ & kHasGroupId; }
  bool has_priority() const { return value_ & kHasPriority; }
  bool has_extensions() const { return value_ & kHasExtensions; }
  bool is_datagram() const { return value_ & kIsDatagram; }
  bool end_of_non_existent_range() const {
    return value_ == kEndOfNonExistentRange;
  }
  bool end_of_unknown_range() const { return value_ == kEndOfUnknownRange; }
  uint64_t value() const { return value_; }

 private:
  MoqtFetchSerialization(uint64_t value) : value_(value) {}
  uint64_t value_ = 0;
};

struct QUICHE_EXPORT MoqtRequestError {
  uint64_t request_id;
  RequestErrorCode error_code;
  std::optional<quic::QuicTimeDelta> retry_interval;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtSubscribe {
  uint64_t request_id;
  FullTrackName full_track_name;
  MessageParameters parameters;
};

struct QUICHE_EXPORT MoqtSubscribeOk {
  uint64_t request_id;
  uint64_t track_alias;
  MessageParameters parameters;
  TrackExtensions extensions;
};

struct QUICHE_EXPORT MoqtUnsubscribe {
  uint64_t request_id;
};

struct QUICHE_EXPORT MoqtPublishDone {
  uint64_t request_id;
  PublishDoneCode status_code;
  uint64_t stream_count;
  std::string error_reason;
};

struct QUICHE_EXPORT MoqtRequestUpdate {
  uint64_t request_id;
  uint64_t existing_request_id;
  MessageParameters parameters;
};

struct QUICHE_EXPORT MoqtPublishNamespace {
  uint64_t request_id;
  TrackNamespace track_namespace;
  MessageParameters parameters;
};

struct QUICHE_EXPORT MoqtRequestOk {
  uint64_t request_id;
  MessageParameters parameters;
};

struct QUICHE_EXPORT MoqtPublishNamespaceDone {
  uint64_t request_id;
};

struct QUICHE_EXPORT MoqtPublishNamespaceCancel {
  uint64_t request_id;
  RequestErrorCode error_code;
  std::string error_reason;
};

struct QUICHE_EXPORT MoqtTrackStatus : public MoqtSubscribe {
  MoqtTrackStatus() = default;
  MoqtTrackStatus(MoqtSubscribe subscribe) : MoqtSubscribe(subscribe) {}
};

struct QUICHE_EXPORT MoqtGoAway {
  std::string new_session_uri;
};

struct QUICHE_EXPORT MoqtSubscribeNamespace {
  uint64_t request_id;
  TrackNamespace track_namespace_prefix;
  SubscribeNamespaceOption subscribe_options;
  MessageParameters parameters;
};

struct QUICHE_EXPORT MoqtNamespace {
  TrackNamespace track_namespace_suffix;
};

struct QUICHE_EXPORT MoqtNamespaceDone {
  TrackNamespace track_namespace_suffix;
};

struct QUICHE_EXPORT MoqtMaxRequestId {
  uint64_t max_request_id;
};

enum class QUICHE_EXPORT FetchType : uint64_t {
  kStandalone = 0x1,
  kRelativeJoining = 0x2,
  kAbsoluteJoining = 0x3,
};

struct StandaloneFetch {
  FullTrackName full_track_name;
  Location start_location;
  Location end_location;

  bool operator==(const StandaloneFetch& other) const = default;
  bool operator!=(const StandaloneFetch& other) const = default;
};

struct JoiningFetchRelative {
  JoiningFetchRelative(uint64_t joining_request_id, uint64_t joining_start)
      : joining_request_id(joining_request_id), joining_start(joining_start) {}
  uint64_t joining_request_id;
  uint64_t joining_start;
  bool operator==(const JoiningFetchRelative& other) const {
    return joining_request_id == other.joining_request_id &&
           joining_start == other.joining_start;
  }
  bool operator!=(const JoiningFetchRelative& other) const {
    return !(*this == other);
  }
};

struct JoiningFetchAbsolute {
  JoiningFetchAbsolute(uint64_t joining_request_id, uint64_t joining_start)
      : joining_request_id(joining_request_id), joining_start(joining_start) {}
  uint64_t joining_request_id;
  uint64_t joining_start;
  bool operator==(const JoiningFetchAbsolute& other) const {
    return joining_request_id == other.joining_request_id &&
           joining_start == other.joining_start;
  }
  bool operator!=(const JoiningFetchAbsolute& other) const {
    return !(*this == other);
  }
};

struct QUICHE_EXPORT MoqtFetch {
  uint64_t request_id;
  std::variant<StandaloneFetch, JoiningFetchRelative, JoiningFetchAbsolute>
      fetch;
  MessageParameters parameters;
};

struct QUICHE_EXPORT MoqtFetchOk {
  uint64_t request_id;
  bool end_of_track;
  Location end_location;
  MessageParameters parameters;
  TrackExtensions extensions;
};

struct QUICHE_EXPORT MoqtFetchCancel {
  uint64_t request_id;
};

struct QUICHE_EXPORT MoqtRequestsBlocked {
  uint64_t max_request_id;
};

struct QUICHE_EXPORT MoqtPublish {
  uint64_t request_id;
  FullTrackName full_track_name;
  uint64_t track_alias;
  MessageParameters parameters;
  TrackExtensions extensions;
};

struct QUICHE_EXPORT MoqtPublishOk {
  uint64_t request_id;
  MessageParameters parameters;
};

// All of the four values in this message are encoded as varints.
// `delta_from_deadline` is encoded as an absolute value, with the lowest bit
// indicating the sign (0 if positive).
struct QUICHE_EXPORT MoqtObjectAck {
  uint64_t subscribe_id;
  uint64_t group_id;
  uint64_t object_id;
  // Positive if the object has been received before the deadline.
  quic::QuicTimeDelta delta_from_deadline = quic::QuicTimeDelta::Zero();
};

// Returns false if the parameters cannot be in |message type|.
MoqtError SetupParametersAllowedByMessage(const SetupParameters& parameters,
                                          MoqtMessageType message_type,
                                          bool webtrans);

std::string MoqtMessageTypeToString(MoqtMessageType message_type);
std::string MoqtDataStreamTypeToString(MoqtDataStreamType type);
std::string MoqtFetchSerializationToString(MoqtFetchSerialization type);
std::string MoqtDatagramTypeToString(MoqtDatagramType type);

std::string MoqtForwardingPreferenceToString(
    MoqtForwardingPreference preference);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
