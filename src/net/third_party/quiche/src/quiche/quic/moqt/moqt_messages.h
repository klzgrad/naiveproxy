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
#include <variant>
#include <vector>

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
#include "quiche/web_transport/web_transport.h"

namespace moqt {

inline constexpr quic::ParsedQuicVersionVector GetMoqtSupportedQuicVersions() {
  return quic::ParsedQuicVersionVector{quic::ParsedQuicVersion::RFCv1()};
}

enum class MoqtVersion : uint64_t {
  kDraft10 = 0xff00000a,
  kUnrecognizedVersionForTests = 0xfe0000ff,
};

inline constexpr MoqtVersion kDefaultMoqtVersion = MoqtVersion::kDraft10;
inline constexpr uint64_t kDefaultInitialMaxSubscribeId = 100;
inline constexpr uint64_t kMinNamespaceElements = 1;
inline constexpr uint64_t kMaxNamespaceElements = 32;

struct QUICHE_EXPORT MoqtSessionParameters {
  // TODO: support multiple versions.
  // TODO: support roles other than PubSub.

  explicit MoqtSessionParameters(quic::Perspective perspective)
      : perspective(perspective), using_webtrans(true) {}
  MoqtSessionParameters(quic::Perspective perspective, std::string path)
      : perspective(perspective),
        using_webtrans(false),
        path(std::move(path)) {}

  MoqtVersion version = kDefaultMoqtVersion;
  quic::Perspective perspective;
  bool using_webtrans;
  std::string path;
  uint64_t max_subscribe_id = kDefaultInitialMaxSubscribeId;
  bool deliver_partial_objects = false;
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
  kMaxSubscribeId = 0x15,
  kFetch = 0x16,
  kFetchCancel = 0x17,
  kFetchOk = 0x18,
  kFetchError = 0x19,
  kSubscribesBlocked = 0x1a,
  kClientSetup = 0x40,
  kServerSetup = 0x41,

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
  kDuplicateTrackAlias = 0x4,
  kParameterLengthMismatch = 0x5,
  kTooManySubscribes = 0x6,
  kGoawayTimeout = 0x10,
  kControlMessageTimeout = 0x11,
  kDataStreamTimeout = 0x12,
};

// Error codes used by MoQT to reset streams.
// TODO: update with spec-defined error codes once those are available, see
// <https://github.com/moq-wg/moq-transport/issues/481>.
inline constexpr webtransport::StreamErrorCode kResetCodeUnknown = 0x00;
inline constexpr webtransport::StreamErrorCode kResetCodeSubscriptionGone =
    0x01;
inline constexpr webtransport::StreamErrorCode kResetCodeTimedOut = 0x02;

enum class QUICHE_EXPORT MoqtSetupParameter : uint64_t {
  kRole = 0x0,
  kPath = 0x1,
  kMaxSubscribeId = 0x2,

  // QUICHE-specific extensions.
  // Indicates support for OACK messages.
  kSupportObjectAcks = 0xbbf1439,
};

enum class QUICHE_EXPORT MoqtTrackRequestParameter : uint64_t {
  kAuthorizationInfo = 0x2,
  kDeliveryTimeout = 0x3,
  kMaxCacheDuration = 0x4,

  // QUICHE-specific extensions.
  kOackWindowSize = 0xbbf1439,
};

// Used for SUBSCRIBE_ERROR, ANNOUNCE_ERROR, ANNOUNCE_CANCEL,
// SUBSCRIBE_ANNOUNCES_ERROR, and FETCH_ERROR.
// TODO(martinduke): Create aliases like FetchErrorCode, etc. to hide the fact
// that these are all the same enum.
enum class QUICHE_EXPORT SubscribeErrorCode : uint64_t {
  kInternalError = 0x0,
  kUnauthorized = 0x1,
  kTimeout = 0x2,
  kNotSupported = 0x3,
  kDoesNotExist = 0x4,     // Can also mean "not interested" or "unknown".
  kInvalidRange = 0x5,     // SUBSCRIBE_ERROR and FETCH_ERROR only.
  kRetryTrackAlias = 0x6,  // SUBSCRIBE_ERROR only.
};

struct MoqtSubscribeErrorReason {
  SubscribeErrorCode error_code;
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
struct FullSequence {
  uint64_t group;
  uint64_t subgroup;
  uint64_t object;
  FullSequence() : FullSequence(0, 0) {}
  // There is a lot of code from before subgroups. Assume there's one subgroup
  // with ID 0 per group.
  FullSequence(uint64_t group, uint64_t object)
      : FullSequence(group, 0, object) {}
  FullSequence(uint64_t group, uint64_t subgroup, uint64_t object)
      : group(group), subgroup(subgroup), object(object) {}
  bool operator==(const FullSequence& other) const {
    return group == other.group && object == other.object;
  }
  // These are temporal ordering comparisons, so subgroup ID doesn't matter.
  bool operator<(const FullSequence& other) const {
    return group < other.group ||
           (group == other.group && object < other.object);
  }
  bool operator<=(const FullSequence& other) const {
    return (group < other.group ||
            (group == other.group && object <= other.object));
  }
  bool operator>(const FullSequence& other) const { return !(*this <= other); }
  FullSequence& operator=(FullSequence other) {
    group = other.group;
    subgroup = other.subgroup;
    object = other.object;
    return *this;
  }
  FullSequence next() const {
    return FullSequence{group, subgroup, object + 1};
  }
  template <typename H>
  friend H AbslHashValue(H h, const FullSequence& m);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FullSequence& sequence) {
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
H AbslHashValue(H h, const FullSequence& m) {
  return H::combine(std::move(h), m.group, m.object);
}

struct QUICHE_EXPORT MoqtClientSetup {
  std::vector<MoqtVersion> supported_versions;
  std::optional<std::string> path;
  std::optional<uint64_t> max_subscribe_id;
  bool supports_object_ack = false;
};

struct QUICHE_EXPORT MoqtServerSetup {
  MoqtVersion selected_version;
  std::optional<uint64_t> max_subscribe_id;
  bool supports_object_ack = false;
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
  kLatestObject = 0x2,
  kAbsoluteStart = 0x3,
  kAbsoluteRange = 0x4,
};

struct QUICHE_EXPORT MoqtSubscribeParameters {
  std::optional<std::string> authorization_info;
  std::optional<quic::QuicTimeDelta> delivery_timeout;
  std::optional<quic::QuicTimeDelta> max_cache_duration;

  // If present, indicates that OBJECT_ACK messages will be sent in response to
  // the objects on the stream. The actual value is informational, and it
  // communicates how many frames the subscriber is willing to buffer, in
  // microseconds.
  std::optional<quic::QuicTimeDelta> object_ack_window;

  bool operator==(const MoqtSubscribeParameters& other) const {
    return authorization_info == other.authorization_info &&
           delivery_timeout == other.delivery_timeout &&
           max_cache_duration == other.max_cache_duration &&
           object_ack_window == other.object_ack_window;
  }
};

struct QUICHE_EXPORT MoqtSubscribe {
  uint64_t subscribe_id;
  uint64_t track_alias;
  FullTrackName full_track_name;
  MoqtPriority subscriber_priority;
  std::optional<MoqtDeliveryOrder> group_order;

  // The combinations of these that have values indicate the filter type.
  // (none): KLatestObject
  // start: kAbsoluteStart
  // start, end_group: kAbsoluteRange (request whole last group)
  // All other combinations are invalid.
  std::optional<FullSequence> start;
  std::optional<uint64_t> end_group;
  // If the mode is kNone, the these are std::nullopt.

  MoqtSubscribeParameters parameters;
};

// Deduce the filter type from the combination of group and object IDs. Returns
// kNone if the state of the subscribe is invalid.
MoqtFilterType GetFilterType(const MoqtSubscribe& message);

struct QUICHE_EXPORT MoqtSubscribeOk {
  uint64_t subscribe_id;
  // The message uses ms, but expires is in us.
  quic::QuicTimeDelta expires = quic::QuicTimeDelta::FromMilliseconds(0);
  MoqtDeliveryOrder group_order;
  // If ContextExists on the wire is zero, largest_id has no value.
  std::optional<FullSequence> largest_id;
  MoqtSubscribeParameters parameters;
};

struct QUICHE_EXPORT MoqtSubscribeError {
  uint64_t subscribe_id;
  SubscribeErrorCode error_code;
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
  uint64_t subscribe_id;
  FullSequence start;
  std::optional<uint64_t> end_group;
  MoqtPriority subscriber_priority;
  MoqtSubscribeParameters parameters;
};

struct QUICHE_EXPORT MoqtAnnounce {
  FullTrackName track_namespace;
  MoqtSubscribeParameters parameters;
};

struct QUICHE_EXPORT MoqtAnnounceOk {
  FullTrackName track_namespace;
};

struct QUICHE_EXPORT MoqtAnnounceError {
  FullTrackName track_namespace;
  SubscribeErrorCode error_code;
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
};

struct QUICHE_EXPORT MoqtAnnounceCancel {
  FullTrackName track_namespace;
  SubscribeErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtTrackStatusRequest {
  FullTrackName full_track_name;
};

struct QUICHE_EXPORT MoqtGoAway {
  std::string new_session_uri;
};

struct QUICHE_EXPORT MoqtSubscribeAnnounces {
  FullTrackName track_namespace;
  MoqtSubscribeParameters parameters;
};

struct QUICHE_EXPORT MoqtSubscribeAnnouncesOk {
  FullTrackName track_namespace;
};

struct QUICHE_EXPORT MoqtSubscribeAnnouncesError {
  FullTrackName track_namespace;
  SubscribeErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtUnsubscribeAnnounces {
  FullTrackName track_namespace;
};

struct QUICHE_EXPORT MoqtMaxSubscribeId {
  uint64_t max_subscribe_id;
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
  FullSequence start_object;  // subgroup is ignored
  uint64_t end_group;
  std::optional<uint64_t> end_object;
  MoqtSubscribeParameters parameters;
};

struct QUICHE_EXPORT MoqtFetchCancel {
  uint64_t subscribe_id;
};

struct QUICHE_EXPORT MoqtFetchOk {
  uint64_t subscribe_id;
  MoqtDeliveryOrder group_order;
  FullSequence largest_id;  // subgroup is ignored
  MoqtSubscribeParameters parameters;
};

struct QUICHE_EXPORT MoqtFetchError {
  uint64_t subscribe_id;
  SubscribeErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtSubscribesBlocked {
  uint64_t max_subscribe_id;
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

std::string MoqtMessageTypeToString(MoqtMessageType message_type);
std::string MoqtDataStreamTypeToString(MoqtDataStreamType type);
std::string MoqtDatagramTypeToString(MoqtDatagramType type);

std::string MoqtForwardingPreferenceToString(
    MoqtForwardingPreference preference);

absl::Status MoqtStreamErrorToStatus(webtransport::StreamErrorCode error_code,
                                     absl::string_view reason_phrase);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
