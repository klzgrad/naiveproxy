// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Structured data for message types in draft-ietf-moq-transport-02.

#ifndef QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
#define QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

inline constexpr quic::ParsedQuicVersionVector GetMoqtSupportedQuicVersions() {
  return quic::ParsedQuicVersionVector{quic::ParsedQuicVersion::RFCv1()};
}

enum class MoqtVersion : uint64_t {
  kDraft12 = 0xff00000c,
  kUnrecognizedVersionForTests = 0xfe0000ff,
};

inline constexpr MoqtVersion kDefaultMoqtVersion = MoqtVersion::kDraft12;
inline constexpr uint64_t kDefaultInitialMaxRequestId = 100;
// TODO(martinduke): Implement an auth token cache.
inline constexpr uint64_t kDefaultMaxAuthTokenCacheSize = 0;
inline constexpr uint64_t kMinNamespaceElements = 1;
inline constexpr uint64_t kMaxNamespaceElements = 32;
inline constexpr size_t kMaxFullTrackNameSize = 1024;

enum AuthTokenType : uint64_t {
  kOutOfBand = 0x0,

  kMaxAuthTokenType = 0x0,
};

enum AuthTokenAliasType : uint64_t {
  kDelete = 0x0,
  kRegister = 0x1,
  kUseAlias = 0x2,
  kUseValue = 0x3,

  kMaxValue = 0x3,
};

struct AuthToken {
  AuthToken(AuthTokenType token_type, absl::string_view token)
      : type(token_type), token(token) {}
  bool operator==(const AuthToken& other) const = default;

  AuthTokenType type;
  std::string token;
};

struct QUICHE_EXPORT MoqtSessionParameters {
  // TODO: support multiple versions.
  MoqtSessionParameters() = default;
  explicit MoqtSessionParameters(quic::Perspective perspective)
      : perspective(perspective), using_webtrans(true) {}
  MoqtSessionParameters(quic::Perspective perspective, std::string path)
      : perspective(perspective),
        using_webtrans(false),
        path(std::move(path)) {}
  MoqtSessionParameters(quic::Perspective perspective, std::string path,
                        uint64_t max_request_id)
      : perspective(perspective),
        using_webtrans(true),
        path(std::move(path)),
        max_request_id(max_request_id) {}
  MoqtSessionParameters(quic::Perspective perspective, uint64_t max_request_id)
      : perspective(perspective), max_request_id(max_request_id) {}
  bool operator==(const MoqtSessionParameters& other) const = default;

  MoqtVersion version = kDefaultMoqtVersion;
  bool deliver_partial_objects = false;
  quic::Perspective perspective = quic::Perspective::IS_SERVER;
  bool using_webtrans = true;
  std::string path = "";
  uint64_t max_request_id = kDefaultInitialMaxRequestId;
  uint64_t max_auth_token_cache_size = kDefaultMaxAuthTokenCacheSize;
  bool support_object_acks = false;
  // TODO(martinduke): Turn authorization_token into structured data.
  std::vector<AuthToken> authorization_token;
};

// The maximum length of a message, excluding any OBJECT payload. This prevents
// DoS attack via forcing the parser to buffer a large message (OBJECT payloads
// are not buffered by the parser).
inline constexpr size_t kMaxMessageHeaderSize = 2048;

class QUICHE_EXPORT MoqtDataStreamType {
 public:
  // Factory functions.
  static std::optional<MoqtDataStreamType> FromValue(uint64_t value) {
    MoqtDataStreamType stream_type(static_cast<StreamType>(value));
    if (stream_type.IsFetch() || stream_type.IsPadding() ||
        stream_type.IsSubgroup()) {
      return stream_type;
    }
    return std::nullopt;
  }
  static MoqtDataStreamType Fetch() {
    return MoqtDataStreamType(StreamType::kFetch);
  }
  static MoqtDataStreamType Padding() {
    return MoqtDataStreamType(StreamType::kPadding);
  }
  static MoqtDataStreamType Subgroup(uint64_t subgroup_id,
                                     uint64_t first_object_id,
                                     bool no_extension_headers) {
    if (subgroup_id == 0) {
      return MoqtDataStreamType(
          no_extension_headers ? StreamType::kSubgroup0NoExtensionHeaders
                               : StreamType::kSubgroup0WithExtensionHeaders);
    }
    if (subgroup_id == first_object_id) {
      return MoqtDataStreamType(
          no_extension_headers
              ? StreamType::kSubgroupFirstObjectNoExtensionHeaders
              : StreamType::kSubgroupFirstObjectWithExtensionHeaders);
    }
    return MoqtDataStreamType(
        no_extension_headers
            ? StreamType::kSubgroupExplicitNoExtensionHeaders
            : StreamType::kSubgroupExplicitWithExtensionHeaders);
  }
  MoqtDataStreamType(const MoqtDataStreamType& other) = default;
  bool IsFetch() const { return value_ == StreamType::kFetch; }
  bool IsPadding() const { return value_ == StreamType::kPadding; }
  bool IsSubgroup() const {
    return value_ >= StreamType::kSubgroup0NoExtensionHeaders &&
           value_ <= StreamType::kSubgroupExplicitWithExtensionHeaders;
  }
  bool IsSubgroupPresent() const {
    return value_ == StreamType::kSubgroupExplicitNoExtensionHeaders ||
           value_ == StreamType::kSubgroupExplicitWithExtensionHeaders;
  }
  bool SubgroupIsZero() const {
    return value_ == StreamType::kSubgroup0NoExtensionHeaders ||
           value_ == StreamType::kSubgroup0WithExtensionHeaders;
  }
  bool SubgroupIsFirstObjectId() const {
    return value_ == StreamType::kSubgroupFirstObjectNoExtensionHeaders ||
           value_ == StreamType::kSubgroupFirstObjectWithExtensionHeaders;
  }
  bool AreExtensionHeadersPresent() const {
    return value_ == StreamType::kSubgroup0WithExtensionHeaders ||
           value_ == StreamType::kSubgroupFirstObjectWithExtensionHeaders ||
           value_ == StreamType::kSubgroupExplicitWithExtensionHeaders;
  }
  uint64_t value() const { return static_cast<uint64_t>(value_); }
  bool operator==(const MoqtDataStreamType& other) const = default;
  enum class StreamType : uint64_t {
    kFetch = 0x05,
    kSubgroup0NoExtensionHeaders = 0x08,
    kSubgroup0WithExtensionHeaders = 0x09,
    kSubgroupFirstObjectNoExtensionHeaders = 0x0a,
    kSubgroupFirstObjectWithExtensionHeaders = 0x0b,
    kSubgroupExplicitNoExtensionHeaders = 0x0c,
    kSubgroupExplicitWithExtensionHeaders = 0x0d,
    kPadding = 0x26d3,
  };

 private:
  explicit MoqtDataStreamType(StreamType value) : value_(value) {}
  const StreamType value_;
};

class QUICHE_EXPORT MoqtDatagramType {
 public:
  MoqtDatagramType(bool has_status, bool has_extension) : value_(0) {
    if (has_status) {
      value_ |= 0x02;
    }
    if (has_extension) {
      value_ |= 0x01;
    }
  }
  static std::optional<MoqtDatagramType> FromValue(uint64_t value) {
    if (value <= 3) {
      return MoqtDatagramType(value);
    }
    return std::nullopt;
  }
  bool has_status() const { return value_ & 0x02; }
  bool has_extension() const { return value_ & 0x01; }
  uint64_t value() const { return value_; }

 private:
  uint64_t value_;
  explicit MoqtDatagramType(uint64_t value) : value_(value) {}
};

enum class QUICHE_EXPORT MoqtMessageType : uint64_t {
  kSubscribeUpdate = 0x02,
  kSubscribe = 0x03,
  kSubscribeOk = 0x04,
  kSubscribeError = 0x05,
  kAnnounce = 0x06,
  kAnnounceOk = 0x7,
  kAnnounceError = 0x08,
  kUnannounce = 0x09,
  kUnsubscribe = 0x0a,
  kSubscribeDone = 0x0b,
  kAnnounceCancel = 0x0c,
  kTrackStatusRequest = 0x0d,
  kTrackStatus = 0x0e,
  kGoAway = 0x10,
  kSubscribeAnnounces = 0x11,
  kSubscribeAnnouncesOk = 0x12,
  kSubscribeAnnouncesError = 0x13,
  kUnsubscribeAnnounces = 0x14,
  kMaxRequestId = 0x15,
  kFetch = 0x16,
  kFetchCancel = 0x17,
  kFetchOk = 0x18,
  kFetchError = 0x19,
  kRequestsBlocked = 0x1a,
  kPublish = 0x1d,
  kPublishOk = 0x1e,
  kPublishError = 0x1f,
  kClientSetup = 0x20,
  kServerSetup = 0x21,

  // QUICHE-specific extensions.

  // kObjectAck (OACK for short) is a frame used by the receiver indicating that
  // it has received and processed the specified object.
  kObjectAck = 0x3184,
};

enum class QUICHE_EXPORT MoqtError : uint64_t {
  kNoError = 0x0,
  kInternalError = 0x1,
  kUnauthorized = 0x2,
  kProtocolViolation = 0x3,
  kInvalidRequestId = 0x4,
  kDuplicateTrackAlias = 0x5,
  kKeyValueFormattingError = 0x6,
  kTooManyRequests = 0x7,
  kInvalidPath = 0x8,
  kMalformedPath = 0x9,
  kGoawayTimeout = 0x10,
  kControlMessageTimeout = 0x11,
  kDataStreamTimeout = 0x12,
  kAuthTokenCacheOverflow = 0x13,
  kDuplicateAuthTokenAlias = 0x14,
  kVersionNegotiationFailed = 0x15,
};

// Error codes used by MoQT to reset streams.
inline constexpr webtransport::StreamErrorCode kResetCodeUnknown = 0x00;
inline constexpr webtransport::StreamErrorCode kResetCodeCanceled = 0x01;
inline constexpr webtransport::StreamErrorCode kResetCodeDeliveryTimeout = 0x02;
inline constexpr webtransport::StreamErrorCode kResetCodeSessionClosed = 0x03;
// TODO(martinduke): This is not in the spec, but is needed. The number might
// change.
inline constexpr webtransport::StreamErrorCode kResetCodeMalformedTrack = 0x04;

enum class QUICHE_EXPORT SetupParameter : uint64_t {
  kPath = 0x1,
  kMaxRequestId = 0x2,
  kAuthorizationToken = 0x3,
  kMaxAuthTokenCacheSize = 0x4,

  // QUICHE-specific extensions.
  // Indicates support for OACK messages.
  kSupportObjectAcks = 0xbbf1438,
};

enum class QUICHE_EXPORT VersionSpecificParameter : uint64_t {
  kDeliveryTimeout = 0x2,
  kAuthorizationToken = 0x3,
  kMaxCacheDuration = 0x4,

  // QUICHE-specific extensions.
  kOackWindowSize = 0xbbf1438,
};

struct VersionSpecificParameters {
  VersionSpecificParameters() = default;
  // Likely parameter combinations.
  VersionSpecificParameters(quic::QuicTimeDelta delivery_timeout,
                            quic::QuicTimeDelta max_cache_duration)
      : delivery_timeout(delivery_timeout),
        max_cache_duration(max_cache_duration) {}
  VersionSpecificParameters(AuthTokenType token_type, absl::string_view token) {
    authorization_token.emplace_back(token_type, token);
  };
  VersionSpecificParameters(quic::QuicTimeDelta delivery_timeout,
                            AuthTokenType token_type, absl::string_view token)
      : delivery_timeout(delivery_timeout) {
    authorization_token.emplace_back(token_type, token);
  }

  // TODO(martinduke): Turn auth_token into structured data.
  std::vector<AuthToken> authorization_token;
  quic::QuicTimeDelta delivery_timeout = quic::QuicTimeDelta::Infinite();
  quic::QuicTimeDelta max_cache_duration = quic::QuicTimeDelta::Infinite();
  std::optional<quic::QuicTimeDelta> oack_window_size;

  bool operator==(const VersionSpecificParameters& other) const = default;
};

// Used for SUBSCRIBE_ERROR, ANNOUNCE_ERROR, ANNOUNCE_CANCEL,
// SUBSCRIBE_ANNOUNCES_ERROR, and FETCH_ERROR.
enum class QUICHE_EXPORT RequestErrorCode : uint64_t {
  kInternalError = 0x0,
  kUnauthorized = 0x1,
  kTimeout = 0x2,
  kNotSupported = 0x3,
  kTrackDoesNotExist = 0x4,          // SUBSCRIBE_ERROR and FETCH_ERROR only.
  kUninterested = 0x4,               // ANNOUNCE_ERROR and ANNOUNCE_CANCEL only.
  kNamespacePrefixUnknown = 0x4,     // SUBSCRIBE_ANNOUNCES_ERROR only.
  kInvalidRange = 0x5,               // SUBSCRIBE_ERROR and FETCH_ERROR only.
  kNamespacePrefixOverlap = 0x5,     // SUBSCRIBE_ANNOUNCES_ERROR only.
  kNoObjects = 0x6,                  // FETCH_ERROR only.
  kInvalidJoiningSubscribeId = 0x7,  // FETCH_ERROR only.
  kMalformedAuthToken = 0x10,
  kExpiredAuthToken = 0x12,
};

struct MoqtSubscribeErrorReason {
  RequestErrorCode error_code;
  std::string reason_phrase;
};
using MoqtAnnounceErrorReason = MoqtSubscribeErrorReason;

class TrackNamespace {
 public:
  explicit TrackNamespace(absl::Span<const absl::string_view> elements);
  explicit TrackNamespace(
      std::initializer_list<const absl::string_view> elements)
      : TrackNamespace(absl::Span<const absl::string_view>(
            std::data(elements), std::size(elements))) {}
  explicit TrackNamespace(absl::string_view ns) : TrackNamespace({ns}) {}
  TrackNamespace() : TrackNamespace({}) {}

  bool IsValid() const {
    return !tuple_.empty() && tuple_.size() <= kMaxNamespaceElements &&
           length_ <= kMaxFullTrackNameSize;
  }
  bool InNamespace(const TrackNamespace& other) const;
  // Check if adding an element will exceed limits, without triggering a
  // bug. Useful for the parser, which has to be robust to malformed data.
  bool CanAddElement(absl::string_view element) {
    return (tuple_.size() < kMaxNamespaceElements &&
            length_ + element.length() <= kMaxFullTrackNameSize);
  }
  void AddElement(absl::string_view element);
  std::string ToString() const;
  // Returns the number of elements in the tuple.
  size_t number_of_elements() const { return tuple_.size(); }
  // Returns the sum of the lengths of all elements in the tuple.
  size_t total_length() const { return length_; }

  auto operator<=>(const TrackNamespace& other) const {
    return std::lexicographical_compare_three_way(
        tuple_.cbegin(), tuple_.cend(), other.tuple_.cbegin(),
        other.tuple_.cend());
  }
  bool operator==(const TrackNamespace&) const = default;

  const std::vector<std::string>& tuple() const { return tuple_; }

  template <typename H>
  friend H AbslHashValue(H h, const TrackNamespace& m) {
    return H::combine(std::move(h), m.tuple_);
  }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const TrackNamespace& track_namespace) {
    sink.Append(track_namespace.ToString());
  }

 private:
  std::vector<std::string> tuple_;
  size_t length_ = 0;  // size in bytes.
};

class FullTrackName {
 public:
  FullTrackName(absl::string_view ns, absl::string_view name)
      : namespace_(ns), name_(name) {
    QUICHE_BUG_IF(Moqt_full_track_name_too_large_01, !IsValid())
        << "Constructing a Full Track Name that is too large.";
  }
  FullTrackName(TrackNamespace ns, absl::string_view name)
      : namespace_(ns), name_(name) {
    QUICHE_BUG_IF(Moqt_full_track_name_too_large_02, !IsValid())
        << "Constructing a Full Track Name that is too large.";
  }
  FullTrackName() = default;

  bool IsValid() const {
    return namespace_.IsValid() && length() <= kMaxFullTrackNameSize;
  }
  const TrackNamespace& track_namespace() const { return namespace_; }
  TrackNamespace& track_namespace() { return namespace_; }
  absl::string_view name() const { return name_; }
  void AddElement(absl::string_view element) {
    return namespace_.AddElement(element);
  }
  std::string ToString() const {
    return absl::StrCat(namespace_.ToString(), "::", name_);
  }
  // Check if the name will exceed limits, without triggering a bug. Useful for
  // the parser, which has to be robust to malformed data.
  bool CanAddName(absl::string_view name) {
    return (namespace_.total_length() + name.length() <= kMaxFullTrackNameSize);
  }
  void set_name(absl::string_view name);
  size_t length() const { return namespace_.total_length() + name_.length(); }

  auto operator<=>(const FullTrackName&) const = default;
  template <typename H>
  friend H AbslHashValue(H h, const FullTrackName& m) {
    return H::combine(std::move(h), m.namespace_.tuple(), m.name_);
  }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FullTrackName& full_track_name) {
    sink.Append(full_track_name.ToString());
  }

 private:
  TrackNamespace namespace_;
  std::string name_ = "";
};

// Location as defined in
// https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#location-structure
struct Location {
  uint64_t group = 0;
  uint64_t object = 0;

  Location() = default;
  Location(uint64_t group, uint64_t object) : group(group), object(object) {}

  // Location order as described in
  // https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#location-structure
  auto operator<=>(const Location&) const = default;

  Location next() const { return Location(group, object + 1); }

  template <typename H>
  friend H AbslHashValue(H h, const Location& m);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Location& sequence) {
    absl::Format(&sink, "(%d; %d)", sequence.group, sequence.object);
  }
};

struct SubgroupPriority {
  uint8_t publisher_priority = 0xf0;
  uint64_t subgroup_id = 0;

  auto operator<=>(const SubgroupPriority&) const = default;
};

template <typename H>
H AbslHashValue(H h, const Location& m) {
  return H::combine(std::move(h), m.group, m.object);
}

// Encodes a list of key-value pairs common to both parameters and extensions.
// If the key is odd, it is a length-prefixed string (which may encode further
// item-specific structure). If the key is even, it is a varint.
// This class does not interpret the semantic meaning of the keys and values,
// although it does accept various uint64_t-based enums to reduce the burden of
// casting on the caller.
class KeyValuePairList {
 public:
  KeyValuePairList() = default;
  size_t size() const { return integer_map_.size() + string_map_.size(); }
  void insert(VersionSpecificParameter key, uint64_t value) {
    insert(static_cast<uint64_t>(key), value);
  }
  void insert(SetupParameter key, uint64_t value) {
    insert(static_cast<uint64_t>(key), value);
  }
  void insert(VersionSpecificParameter key, absl::string_view value) {
    insert(static_cast<uint64_t>(key), value);
  }
  void insert(SetupParameter key, absl::string_view value) {
    insert(static_cast<uint64_t>(key), value);
  }
  void insert(uint64_t key, absl::string_view value);
  void insert(uint64_t key, uint64_t value);
  size_t count(VersionSpecificParameter key) const {
    return count(static_cast<uint64_t>(key));
  }
  size_t count(SetupParameter key) const {
    return count(static_cast<uint64_t>(key));
  }
  bool contains(VersionSpecificParameter key) const {
    return contains(static_cast<uint64_t>(key));
  }
  bool contains(SetupParameter key) const {
    return contains(static_cast<uint64_t>(key));
  }
  // If either of these callbacks returns false, ForEach will return early.
  using IntCallback = quiche::UnretainedCallback<bool(uint64_t, uint64_t)>;
  using StringCallback =
      quiche::UnretainedCallback<bool(uint64_t, absl::string_view)>;
  // Iterates through the whole list, and executes int_callback for each integer
  // value and string_callback for each string value.
  bool ForEach(IntCallback int_callback, StringCallback string_callback) const {
    for (const auto& [key, value] : integer_map_) {
      if (!int_callback(key, value)) {
        return false;
      }
    }
    for (const auto& [key, value] : string_map_) {
      if (!string_callback(key, value)) {
        return false;
      }
    }
    return true;
  }
  std::vector<uint64_t> GetIntegers(VersionSpecificParameter key) const {
    return GetIntegers(static_cast<uint64_t>(key));
  }
  std::vector<uint64_t> GetIntegers(SetupParameter key) const {
    return GetIntegers(static_cast<uint64_t>(key));
  }
  std::vector<absl::string_view> GetStrings(
      VersionSpecificParameter key) const {
    return GetStrings(static_cast<uint64_t>(key));
  }
  std::vector<absl::string_view> GetStrings(SetupParameter key) const {
    return GetStrings(static_cast<uint64_t>(key));
  }
  void clear() {
    integer_map_.clear();
    string_map_.clear();
  }

 private:
  size_t count(uint64_t key) const;
  bool contains(uint64_t key) const;
  std::vector<uint64_t> GetIntegers(uint64_t key) const;
  std::vector<absl::string_view> GetStrings(uint64_t key) const;
  absl::btree_multimap<uint64_t, uint64_t> integer_map_;
  absl::btree_multimap<uint64_t, std::string> string_map_;
};

// TODO(martinduke): Collapse both Setup messages into MoqtSessionParameters.
struct QUICHE_EXPORT MoqtClientSetup {
  std::vector<MoqtVersion> supported_versions;
  MoqtSessionParameters parameters;
};

struct QUICHE_EXPORT MoqtServerSetup {
  MoqtVersion selected_version;
  MoqtSessionParameters parameters;
};

// These codes do not appear on the wire.
enum class QUICHE_EXPORT MoqtForwardingPreference {
  kSubgroup,
  kDatagram,
};

enum class QUICHE_EXPORT MoqtObjectStatus : uint64_t {
  kNormal = 0x0,
  kObjectDoesNotExist = 0x1,
  kEndOfGroup = 0x3,
  kEndOfTrack = 0x4,
  kInvalidObjectStatus = 0x5,
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
  uint64_t subgroup_id;
  uint64_t payload_length;
};

enum class QUICHE_EXPORT MoqtFilterType : uint64_t {
  kNone = 0x0,
  kNextGroupStart = 0x1,
  kLatestObject = 0x2,
  kAbsoluteStart = 0x3,
  kAbsoluteRange = 0x4,
};

struct QUICHE_EXPORT MoqtSubscribe {
  uint64_t request_id;
  FullTrackName full_track_name;
  MoqtPriority subscriber_priority;
  std::optional<MoqtDeliveryOrder> group_order;
  bool forward;
  MoqtFilterType filter_type;
  std::optional<Location> start;
  std::optional<uint64_t> end_group;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtSubscribeOk {
  uint64_t request_id;
  uint64_t track_alias;
  // The message uses ms, but expires is in us.
  quic::QuicTimeDelta expires = quic::QuicTimeDelta::FromMilliseconds(0);
  MoqtDeliveryOrder group_order;
  // If ContextExists on the wire is zero, largest_id has no value.
  std::optional<Location> largest_location;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtSubscribeError {
  uint64_t request_id;
  RequestErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtUnsubscribe {
  uint64_t request_id;
};

enum class QUICHE_EXPORT SubscribeDoneCode : uint64_t {
  kInternalError = 0x0,
  kUnauthorized = 0x1,
  kTrackEnded = 0x2,
  kSubscriptionEnded = 0x3,
  kGoingAway = 0x4,
  kExpired = 0x5,
  kTooFarBehind = 0x6,
  kMalformedTrack = 0x7,
};

struct QUICHE_EXPORT MoqtSubscribeDone {
  uint64_t request_id;
  SubscribeDoneCode status_code;
  uint64_t stream_count;
  std::string error_reason;
};

struct QUICHE_EXPORT MoqtSubscribeUpdate {
  uint64_t request_id;
  Location start;
  std::optional<uint64_t> end_group;
  MoqtPriority subscriber_priority;
  bool forward;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtAnnounce {
  uint64_t request_id;
  TrackNamespace track_namespace;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtAnnounceOk {
  uint64_t request_id;
};

struct QUICHE_EXPORT MoqtAnnounceError {
  uint64_t request_id;
  RequestErrorCode error_code;
  std::string error_reason;
};

struct QUICHE_EXPORT MoqtUnannounce {
  TrackNamespace track_namespace;
};

struct QUICHE_EXPORT MoqtAnnounceCancel {
  TrackNamespace track_namespace;
  RequestErrorCode error_code;
  std::string error_reason;
};

enum class QUICHE_EXPORT MoqtTrackStatusCode : uint64_t {
  kInProgress = 0x0,
  kDoesNotExist = 0x1,
  kNotYetBegun = 0x2,
  kFinished = 0x3,
  kStatusNotAvailable = 0x4,
};

inline bool DoesTrackStatusImplyHavingData(MoqtTrackStatusCode code) {
  switch (code) {
    case MoqtTrackStatusCode::kInProgress:
    case MoqtTrackStatusCode::kFinished:
      return true;
    case MoqtTrackStatusCode::kDoesNotExist:
    case MoqtTrackStatusCode::kNotYetBegun:
    case MoqtTrackStatusCode::kStatusNotAvailable:
      return false;
  }
  return false;
}

struct QUICHE_EXPORT MoqtTrackStatusRequest {
  uint64_t request_id;
  FullTrackName full_track_name;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtTrackStatus {
  uint64_t request_id;
  MoqtTrackStatusCode status_code;
  Location largest_location;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtGoAway {
  std::string new_session_uri;
};

struct QUICHE_EXPORT MoqtSubscribeAnnounces {
  uint64_t request_id;
  TrackNamespace track_namespace;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtSubscribeAnnouncesOk {
  uint64_t request_id;
};

struct QUICHE_EXPORT MoqtSubscribeAnnouncesError {
  uint64_t request_id;
  RequestErrorCode error_code;
  std::string error_reason;
};

struct QUICHE_EXPORT MoqtUnsubscribeAnnounces {
  TrackNamespace track_namespace;
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
  StandaloneFetch() = default;
  StandaloneFetch(FullTrackName full_track_name, Location start_object,
                  uint64_t end_group, std::optional<uint64_t> end_object)
      : full_track_name(full_track_name),
        start_object(start_object),
        end_group(end_group),
        end_object(end_object) {}
  FullTrackName full_track_name;
  Location start_object;  // subgroup is ignored
  uint64_t end_group;
  std::optional<uint64_t> end_object;
  bool operator==(const StandaloneFetch& other) const {
    return full_track_name == other.full_track_name &&
           start_object == other.start_object && end_group == other.end_group &&
           end_object == other.end_object;
  }
  bool operator!=(const StandaloneFetch& other) const {
    return !(*this == other);
  }
};

struct JoiningFetchRelative {
  JoiningFetchRelative(uint64_t joining_subscribe_id, uint64_t joining_start)
      : joining_subscribe_id(joining_subscribe_id),
        joining_start(joining_start) {}
  uint64_t joining_subscribe_id;
  uint64_t joining_start;
  bool operator==(const JoiningFetchRelative& other) const {
    return joining_subscribe_id == other.joining_subscribe_id &&
           joining_start == other.joining_start;
  }
  bool operator!=(const JoiningFetchRelative& other) const {
    return !(*this == other);
  }
};

struct JoiningFetchAbsolute {
  JoiningFetchAbsolute(uint64_t joining_subscribe_id, uint64_t joining_start)
      : joining_subscribe_id(joining_subscribe_id),
        joining_start(joining_start) {}
  uint64_t joining_subscribe_id;
  uint64_t joining_start;
  bool operator==(const JoiningFetchAbsolute& other) const {
    return joining_subscribe_id == other.joining_subscribe_id &&
           joining_start == other.joining_start;
  }
  bool operator!=(const JoiningFetchAbsolute& other) const {
    return !(*this == other);
  }
};

struct QUICHE_EXPORT MoqtFetch {
  uint64_t request_id;
  MoqtPriority subscriber_priority;
  std::optional<MoqtDeliveryOrder> group_order;
  std::variant<StandaloneFetch, JoiningFetchRelative, JoiningFetchAbsolute>
      fetch;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtFetchOk {
  uint64_t request_id;
  MoqtDeliveryOrder group_order;
  bool end_of_track;
  Location end_location;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtFetchError {
  uint64_t request_id;
  RequestErrorCode error_code;
  std::string error_reason;
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
  MoqtDeliveryOrder group_order;
  std::optional<Location> largest_location;
  bool forward;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtPublishOk {
  uint64_t request_id;
  bool forward;
  MoqtPriority subscriber_priority;
  MoqtDeliveryOrder group_order;
  MoqtFilterType filter_type;
  std::optional<Location> start;
  std::optional<uint64_t> end_group;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtPublishError {
  uint64_t request_id;
  RequestErrorCode error_code;
  std::string error_reason;
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

RequestErrorCode StatusToRequestErrorCode(absl::Status status);
absl::StatusCode RequestErrorCodeToStatusCode(RequestErrorCode error_code);
absl::Status RequestErrorCodeToStatus(RequestErrorCode error_code,
                                      absl::string_view reason_phrase);

// Returns an error if the parameters are malformed or otherwise violate the
// spec. |perspective| is the consumer of the message, not the sender.
MoqtError ValidateSetupParameters(const KeyValuePairList& parameters,
                                  bool webtrans, quic::Perspective perspective);
// Returns false if the parameters contain a protocol violation, or a
// parameter cannot be in |message type|. Does not validate the internal
// structure of Authorization Token values.
bool ValidateVersionSpecificParameters(const KeyValuePairList& parameters,
                                       MoqtMessageType message_type);

std::string MoqtMessageTypeToString(MoqtMessageType message_type);
std::string MoqtDataStreamTypeToString(MoqtDataStreamType type);
std::string MoqtDatagramTypeToString(MoqtDatagramType type);

std::string MoqtForwardingPreferenceToString(
    MoqtForwardingPreference preference);

absl::Status MoqtStreamErrorToStatus(webtransport::StreamErrorCode error_code,
                                     absl::string_view reason_phrase);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
