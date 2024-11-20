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

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace moqt {

inline constexpr quic::ParsedQuicVersionVector GetMoqtSupportedQuicVersions() {
  return quic::ParsedQuicVersionVector{quic::ParsedQuicVersion::RFCv1()};
}

enum class MoqtVersion : uint64_t {
  kDraft06 = 0xff000006,
  kUnrecognizedVersionForTests = 0xfe0000ff,
};

inline constexpr MoqtVersion kDefaultMoqtVersion = MoqtVersion::kDraft06;
inline constexpr uint64_t kDefaultInitialMaxSubscribeId = 100;

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
  kObjectDatagram = 0x01,
  kStreamHeaderTrack = 0x02,
  kStreamHeaderSubgroup = 0x04,

  // Currently QUICHE-specific.  All data on a kPadding stream is ignored.
  kPadding = 0x26d3,
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
  kSubscribeNamespace = 0x11,
  kSubscribeNamespaceOk = 0x12,
  kSubscribeNamespaceError = 0x13,
  kUnsubscribeNamespace = 0x14,
  kMaxSubscribeId = 0x15,
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
};

// Error codes used by MoQT to reset streams.
// TODO: update with spec-defined error codes once those are available, see
// <https://github.com/moq-wg/moq-transport/issues/481>.
inline constexpr uint64_t kResetCodeUnknown = 0x00;
inline constexpr uint64_t kResetCodeSubscriptionGone = 0x01;
inline constexpr uint64_t kResetCodeTimedOut = 0x02;

enum class QUICHE_EXPORT MoqtRole : uint64_t {
  kPublisher = 0x1,
  kSubscriber = 0x2,
  kPubSub = 0x3,
  kRoleMax = 0x3,
};

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

// TODO: those are non-standard; add standard error codes once those exist, see
// <https://github.com/moq-wg/moq-transport/issues/393>.
enum class MoqtAnnounceErrorCode : uint64_t {
  kInternalError = 0,
  kAnnounceNotSupported = 1,
};

struct MoqtAnnounceErrorReason {
  MoqtAnnounceErrorCode error_code;
  std::string reason_phrase;
};

// Full track name represents a tuple of name elements. All higher order
// elements MUST be present, but lower-order ones (like the name) can be
// omitted.
class FullTrackName {
 public:
  explicit FullTrackName(absl::Span<const absl::string_view> elements);
  explicit FullTrackName(
      std::initializer_list<const absl::string_view> elements)
      : FullTrackName(absl::Span<const absl::string_view>(
            std::data(elements), std::size(elements))) {}
  explicit FullTrackName(absl::string_view ns, absl::string_view name)
      : FullTrackName({ns, name}) {}
  FullTrackName() : FullTrackName({}) {}

  std::string ToString() const;

  void AddElement(absl::string_view element) {
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
};

template <typename H>
H AbslHashValue(H h, const FullSequence& m) {
  return H::combine(std::move(h), m.group, m.object);
}

struct QUICHE_EXPORT MoqtClientSetup {
  std::vector<MoqtVersion> supported_versions;
  std::optional<MoqtRole> role;
  std::optional<std::string> path;
  std::optional<uint64_t> max_subscribe_id;
  bool supports_object_ack = false;
};

struct QUICHE_EXPORT MoqtServerSetup {
  MoqtVersion selected_version;
  std::optional<MoqtRole> role;
  std::optional<uint64_t> max_subscribe_id;
  bool supports_object_ack = false;
};

// These codes do not appear on the wire.
enum class QUICHE_EXPORT MoqtForwardingPreference {
  kTrack,
  kSubgroup,
  kDatagram,
};

enum class QUICHE_EXPORT MoqtObjectStatus : uint64_t {
  kNormal = 0x0,
  kObjectDoesNotExist = 0x1,
  kGroupDoesNotExist = 0x2,
  kEndOfGroup = 0x3,
  kEndOfTrack = 0x4,
  kEndOfSubgroup = 0x5,
  kInvalidObjectStatus = 0x6,
};

MoqtObjectStatus IntegerToObjectStatus(uint64_t integer);

// The data contained in every Object message, although the message type
// implies some of the values.
struct QUICHE_EXPORT MoqtObject {
  uint64_t subscribe_id;
  uint64_t track_alias;
  uint64_t group_id;
  uint64_t object_id;
  MoqtPriority publisher_priority;
  MoqtObjectStatus object_status;
  MoqtForwardingPreference forwarding_preference;
  std::optional<uint64_t> subgroup_id;
  uint64_t payload_length;
};

enum class QUICHE_EXPORT MoqtFilterType : uint64_t {
  kNone = 0x0,
  kLatestGroup = 0x1,
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
  // SG: Start Group; SO: Start Object; EG: End Group; EO: End Object;
  // (none): KLatestObject
  // SO: kLatestGroup (must be zero)
  // SG, SO: kAbsoluteStart
  // SG, SO, EG, EO: kAbsoluteRange
  // SG, SO, EG: kAbsoluteRange (request whole last group)
  // All other combinations are invalid.
  std::optional<uint64_t> start_group;
  std::optional<uint64_t> start_object;
  std::optional<uint64_t> end_group;
  std::optional<uint64_t> end_object;
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

enum class QUICHE_EXPORT SubscribeErrorCode : uint64_t {
  kInternalError = 0x0,
  kInvalidRange = 0x1,
  kRetryTrackAlias = 0x2,
  kTrackDoesNotExist = 0x3,
  kUnauthorized = 0x4,
  kTimeout = 0x5,
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
  kUnsubscribed = 0x0,
  kInternalError = 0x1,
  kUnauthorized = 0x2,
  kTrackEnded = 0x3,
  kSubscriptionEnded = 0x4,
  kGoingAway = 0x5,
  kExpired = 0x6,
};

struct QUICHE_EXPORT MoqtSubscribeDone {
  uint64_t subscribe_id;
  SubscribeDoneCode status_code;
  std::string reason_phrase;
  std::optional<FullSequence> final_id;
};

struct QUICHE_EXPORT MoqtSubscribeUpdate {
  uint64_t subscribe_id;
  uint64_t start_group;
  uint64_t start_object;
  std::optional<uint64_t> end_group;
  std::optional<uint64_t> end_object;
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
  MoqtAnnounceErrorCode error_code;
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
  // TODO: What namespace is this error code in?
  uint64_t error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtTrackStatusRequest {
  FullTrackName full_track_name;
};

struct QUICHE_EXPORT MoqtGoAway {
  std::string new_session_uri;
};

struct QUICHE_EXPORT MoqtSubscribeNamespace {
  FullTrackName track_namespace;
  MoqtSubscribeParameters parameters;
};

struct QUICHE_EXPORT MoqtSubscribeNamespaceOk {
  FullTrackName track_namespace;
};

struct QUICHE_EXPORT MoqtSubscribeNamespaceError {
  FullTrackName track_namespace;
  MoqtAnnounceErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtUnsubscribeNamespace {
  FullTrackName track_namespace;
};

struct QUICHE_EXPORT MoqtMaxSubscribeId {
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

std::string MoqtForwardingPreferenceToString(
    MoqtForwardingPreference preference);

MoqtForwardingPreference GetForwardingPreference(MoqtDataStreamType type);

MoqtDataStreamType GetMessageTypeForForwardingPreference(
    MoqtForwardingPreference preference);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
