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
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
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
  kDraft11 = 0xff00000b,
  kUnrecognizedVersionForTests = 0xfe0000ff,
};

inline constexpr MoqtVersion kDefaultMoqtVersion = MoqtVersion::kDraft11;
inline constexpr uint64_t kDefaultInitialMaxRequestId = 100;
// TODO(martinduke): Implement an auth token cache.
inline constexpr uint64_t kDefaultMaxAuthTokenCacheSize = 0;
inline constexpr uint64_t kMinNamespaceElements = 1;
inline constexpr uint64_t kMaxNamespaceElements = 32;

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
  bool operator==(const MoqtSessionParameters& other) {
    return version == other.version &&
           deliver_partial_objects == other.deliver_partial_objects &&
           perspective == other.perspective &&
           using_webtrans == other.using_webtrans && path == other.path &&
           max_request_id == other.max_request_id &&
           max_auth_token_cache_size == other.max_auth_token_cache_size &&
           support_object_acks == other.support_object_acks;
  }

  MoqtVersion version = kDefaultMoqtVersion;
  bool deliver_partial_objects = false;
  quic::Perspective perspective = quic::Perspective::IS_SERVER;
  bool using_webtrans = true;
  std::string path = "";
  uint64_t max_request_id = kDefaultInitialMaxRequestId;
  uint64_t max_auth_token_cache_size = kDefaultMaxAuthTokenCacheSize;
  bool support_object_acks = false;
};

// The maximum length of a message, excluding any OBJECT payload. This prevents
// DoS attack via forcing the parser to buffer a large message (OBJECT payloads
// are not buffered by the parser).
inline constexpr size_t kMaxMessageHeaderSize = 2048;

enum class QUICHE_EXPORT MoqtDataStreamType : uint64_t {
  kStreamHeaderSubgroup = 0x04,
  kStreamHeaderFetch = 0x05,

  // Currently QUICHE-specific.  All data on a kPadding stream is ignored.
  kPadding = 0x26d3,
};

enum class QUICHE_EXPORT MoqtDatagramType : uint64_t {
  kObject = 0x01,
  kObjectStatus = 0x02,
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
// TODO: update with spec-defined error codes once those are available, see
// <https://github.com/moq-wg/moq-transport/issues/481>.
inline constexpr webtransport::StreamErrorCode kResetCodeUnknown = 0x00;
inline constexpr webtransport::StreamErrorCode kResetCodeSubscriptionGone =
    0x01;
inline constexpr webtransport::StreamErrorCode kResetCodeTimedOut = 0x02;

enum class QUICHE_EXPORT SetupParameter : uint64_t {
  kPath = 0x1,
  kMaxRequestId = 0x2,
  kMaxAuthTokenCacheSize = 0x4,

  // QUICHE-specific extensions.
  // Indicates support for OACK messages.
  kSupportObjectAcks = 0xbbf1438,
};

enum class QUICHE_EXPORT VersionSpecificParameter : uint64_t {
  kAuthorizationToken = 0x1,
  kDeliveryTimeout = 0x2,
  kMaxCacheDuration = 0x4,

  // QUICHE-specific extensions.
  kOackWindowSize = 0xbbf1438,
};

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
  bool operator==(const AuthToken& other) const {
    return type == other.type && token == other.token;
  }
  AuthTokenType type;
  std::string token;
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

  bool operator==(const VersionSpecificParameters& other) const {
    return authorization_token == other.authorization_token &&
           delivery_timeout == other.delivery_timeout &&
           max_cache_duration == other.max_cache_duration &&
           oack_window_size == other.oack_window_size;
  }
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
  kRetryTrackAlias = 0x6,            // SUBSCRIBE_ERROR only.
  kNoObjects = 0x6,                  // FETCH_ERROR only.
  kInvalidJoiningSubscribeId = 0x7,  // FETCH_ERROR only.
  kMalformedAuthToken = 0x10,
  kUnknownAuthTokenAlias = 0x11,
  kExpiredAuthToken = 0x12,
};

struct MoqtSubscribeErrorReason {
  RequestErrorCode error_code;
  std::string reason_phrase;
};
using MoqtAnnounceErrorReason = MoqtSubscribeErrorReason;

// Full track name represents a tuple of name elements. All higher order
// elements MUST be present, but lower-order ones (like the name) can be
// omitted.
class FullTrackName {
 public:
  explicit FullTrackName(absl::Span<const absl::string_view> elements);
  explicit FullTrackName(
      std::initializer_list<const absl::string_view> elements)
      : FullTrackName(absl::Span<const absl::string_view>(
            std::data(elements), std::size(elements))) {
    QUICHE_BUG_IF(Moqt_namespace_too_large_02,
                  elements.size() > (kMaxNamespaceElements + 1))
        << "Constructing a namespace that is too large.";
  }
  explicit FullTrackName(absl::string_view ns, absl::string_view name)
      : FullTrackName({ns, name}) {}
  FullTrackName() : FullTrackName({}) {}

  std::string ToString() const;

  void AddElement(absl::string_view element) {
    QUICHE_BUG_IF(Moqt_namespace_too_large_01,
                  tuple_.size() > (kMaxNamespaceElements + 1))
        << "Constructing a namespace that is too large.";
    tuple_.push_back(std::string(element));
  }
  // Remove the last element to convert a name to a namespace.
  void NameToNamespace() { tuple_.pop_back(); }
  // returns true is |this| is a subdomain of |other|.
  bool InNamespace(const FullTrackName& other) const {
    if (tuple_.size() < other.tuple_.size()) {
      return false;
    }
    for (int i = 0; i < other.tuple_.size(); ++i) {
      if (tuple_[i] != other.tuple_[i]) {
        return false;
      }
    }
    return true;
  }
  absl::Span<const std::string> tuple() const {
    return absl::MakeConstSpan(tuple_);
  }

  bool operator==(const FullTrackName& other) const;
  bool operator<(const FullTrackName& other) const;

  template <typename H>
  friend H AbslHashValue(H h, const FullTrackName& m) {
    return H::combine(std::move(h), m.tuple_);
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FullTrackName& track_name) {
    sink.Append(track_name.ToString());
  }

  bool empty() const { return tuple_.empty(); }

 private:
  absl::InlinedVector<std::string, 2> tuple_;
};

// These are absolute sequence numbers.
struct Location {
  uint64_t group;
  uint64_t subgroup;
  uint64_t object;
  Location() : Location(0, 0) {}
  // There is a lot of code from before subgroups. Assume there's one subgroup
  // with ID 0 per group.
  Location(uint64_t group, uint64_t object) : Location(group, 0, object) {}
  Location(uint64_t group, uint64_t subgroup, uint64_t object)
      : group(group), subgroup(subgroup), object(object) {}
  bool operator==(const Location& other) const {
    return group == other.group && object == other.object;
  }
  // These are temporal ordering comparisons, so subgroup ID doesn't matter.
  bool operator<(const Location& other) const {
    return group < other.group ||
           (group == other.group && object < other.object);
  }
  bool operator<=(const Location& other) const {
    return (group < other.group ||
            (group == other.group && object <= other.object));
  }
  bool operator>(const Location& other) const { return !(*this <= other); }
  Location& operator=(Location other) {
    group = other.group;
    subgroup = other.subgroup;
    object = other.object;
    return *this;
  }
  Location next() const { return Location{group, subgroup, object + 1}; }
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

  bool operator==(const SubgroupPriority& other) const {
    return publisher_priority == other.publisher_priority &&
           subgroup_id == other.subgroup_id;
  }
  bool operator<(const SubgroupPriority& other) const {
    return publisher_priority < other.publisher_priority ||
           (publisher_priority == other.publisher_priority &&
            subgroup_id < other.subgroup_id);
  }
  bool operator<=(const SubgroupPriority& other) const {
    return (publisher_priority < other.publisher_priority ||
            (publisher_priority == other.publisher_priority &&
             subgroup_id <= other.subgroup_id));
  }
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
  kGroupDoesNotExist = 0x2,
  kEndOfGroup = 0x3,
  kEndOfTrackAndGroup = 0x4,
  kEndOfTrack = 0x5,
  kInvalidObjectStatus = 0x6,
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
  std::optional<uint64_t> subgroup_id;
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
  uint64_t track_alias;
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
  uint64_t track_alias;
};

struct QUICHE_EXPORT MoqtUnsubscribe {
  uint64_t subscribe_id;
};

enum class QUICHE_EXPORT SubscribeDoneCode : uint64_t {
  kInternalError = 0x0,
  kUnauthorized = 0x1,
  kTrackEnded = 0x2,
  kSubscriptionEnded = 0x3,
  kGoingAway = 0x4,
  kExpired = 0x5,
  kTooFarBehind = 0x6,
};

struct QUICHE_EXPORT MoqtSubscribeDone {
  uint64_t subscribe_id;
  SubscribeDoneCode status_code;
  uint64_t stream_count;
  std::string reason_phrase;
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
  FullTrackName track_namespace;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtAnnounceOk {
  FullTrackName track_namespace;
};

struct QUICHE_EXPORT MoqtAnnounceError {
  FullTrackName track_namespace;
  RequestErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtUnannounce {
  FullTrackName track_namespace;
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

struct QUICHE_EXPORT MoqtTrackStatus {
  FullTrackName full_track_name;
  MoqtTrackStatusCode status_code;
  uint64_t last_group;
  uint64_t last_object;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtAnnounceCancel {
  FullTrackName track_namespace;
  RequestErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtTrackStatusRequest {
  FullTrackName full_track_name;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtGoAway {
  std::string new_session_uri;
};

struct QUICHE_EXPORT MoqtSubscribeAnnounces {
  FullTrackName track_namespace;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtSubscribeAnnouncesOk {
  FullTrackName track_namespace;
};

struct QUICHE_EXPORT MoqtSubscribeAnnouncesError {
  FullTrackName track_namespace;
  RequestErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtUnsubscribeAnnounces {
  FullTrackName track_namespace;
};

struct QUICHE_EXPORT MoqtMaxRequestId {
  uint64_t max_request_id;
};

enum class QUICHE_EXPORT FetchType : uint64_t {
  kStandalone = 0x1,
  kJoining = 0x2,
};

struct JoiningFetch {
  JoiningFetch(uint64_t joining_subscribe_id, uint64_t preceding_group_offset)
      : joining_subscribe_id(joining_subscribe_id),
        preceding_group_offset(preceding_group_offset) {}
  uint64_t joining_subscribe_id;
  uint64_t preceding_group_offset;
};

struct QUICHE_EXPORT MoqtFetch {
  uint64_t fetch_id;
  MoqtPriority subscriber_priority;
  std::optional<MoqtDeliveryOrder> group_order;
  // If joining_fetch has a value, then the parser will not populate the name
  // and ranges. The session will populate them instead.
  std::optional<JoiningFetch> joining_fetch;
  FullTrackName full_track_name;
  Location start_object;  // subgroup is ignored
  uint64_t end_group;
  std::optional<uint64_t> end_object;
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtFetchCancel {
  uint64_t subscribe_id;
};

struct QUICHE_EXPORT MoqtFetchOk {
  uint64_t subscribe_id;
  MoqtDeliveryOrder group_order;
  Location largest_id;  // subgroup is ignored
  VersionSpecificParameters parameters;
};

struct QUICHE_EXPORT MoqtFetchError {
  uint64_t subscribe_id;
  RequestErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtRequestsBlocked {
  uint64_t max_request_id;
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
