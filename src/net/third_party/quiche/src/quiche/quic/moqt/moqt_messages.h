// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Structured data for message types in draft-ietf-moq-transport-02.

#ifndef QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
#define QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
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
  kDraft05 = 0xff000005,
  kUnrecognizedVersionForTests = 0xfe0000ff,
};

inline constexpr MoqtVersion kDefaultMoqtVersion = MoqtVersion::kDraft05;

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
  bool deliver_partial_objects = false;
  bool support_object_acks = false;
};

// The maximum length of a message, excluding any OBJECT payload. This prevents
// DoS attack via forcing the parser to buffer a large message (OBJECT payloads
// are not buffered by the parser).
inline constexpr size_t kMaxMessageHeaderSize = 2048;

enum class QUICHE_EXPORT MoqtMessageType : uint64_t {
  kObjectStream = 0x00,
  kObjectDatagram = 0x01,
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
  kClientSetup = 0x40,
  kServerSetup = 0x41,
  kStreamHeaderTrack = 0x50,
  kStreamHeaderGroup = 0x51,

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

  // QUICHE-specific extensions.
  // Indicates support for OACK messages.
  kSupportObjectAcks = 0xbbf1439,
};

enum class QUICHE_EXPORT MoqtTrackRequestParameter : uint64_t {
  kAuthorizationInfo = 0x2,

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

struct FullTrackName {
  std::string track_namespace;
  std::string track_name;
  FullTrackName(absl::string_view ns, absl::string_view name)
      : track_namespace(ns), track_name(name) {}
  bool operator==(const FullTrackName& other) const {
    return track_namespace == other.track_namespace &&
           track_name == other.track_name;
  }
  bool operator<(const FullTrackName& other) const {
    return track_namespace < other.track_namespace ||
           (track_namespace == other.track_namespace &&
            track_name < other.track_name);
  }
  FullTrackName& operator=(const FullTrackName& other) {
    track_namespace = other.track_namespace;
    track_name = other.track_name;
    return *this;
  }
  template <typename H>
  friend H AbslHashValue(H h, const FullTrackName& m);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FullTrackName& track_name) {
    absl::Format(&sink, "(%s; %s)", track_name.track_namespace,
                 track_name.track_name);
  }
};

template <typename H>
H AbslHashValue(H h, const FullTrackName& m) {
  return H::combine(std::move(h), m.track_namespace, m.track_name);
}

// These are absolute sequence numbers.
struct FullSequence {
  uint64_t group = 0;
  uint64_t object = 0;
  bool operator==(const FullSequence& other) const {
    return group == other.group && object == other.object;
  }
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
  FullSequence next() const { return FullSequence{group, object + 1}; }
  template <typename H>
  friend H AbslHashValue(H h, const FullSequence& m);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FullSequence& sequence) {
    absl::Format(&sink, "(%d; %d)", sequence.group, sequence.object);
  }
};

template <typename H>
H AbslHashValue(H h, const FullSequence& m) {
  return H::combine(std::move(h), m.group, m.object);
}

struct QUICHE_EXPORT MoqtClientSetup {
  std::vector<MoqtVersion> supported_versions;
  std::optional<MoqtRole> role;
  std::optional<std::string> path;
  bool supports_object_ack = false;
};

struct QUICHE_EXPORT MoqtServerSetup {
  MoqtVersion selected_version;
  std::optional<MoqtRole> role;
  bool supports_object_ack = false;
};

// These codes do not appear on the wire.
enum class QUICHE_EXPORT MoqtForwardingPreference : uint8_t {
  kTrack = 0x0,
  kGroup = 0x1,
  kObject = 0x2,
  kDatagram = 0x3,
};

enum class QUICHE_EXPORT MoqtObjectStatus : uint64_t {
  kNormal = 0x0,
  kObjectDoesNotExist = 0x1,
  kGroupDoesNotExist = 0x2,
  kEndOfGroup = 0x3,
  kEndOfTrack = 0x4,
  kInvalidObjectStatus = 0x5,
};

MoqtObjectStatus IntegerToObjectStatus(uint64_t integer);

// The data contained in every Object message, although the message type
// implies some of the values. |payload_length| has no value if the length
// is unknown (because it runs to the end of the stream.)
struct QUICHE_EXPORT MoqtObject {
  uint64_t subscribe_id;
  uint64_t track_alias;
  uint64_t group_id;
  uint64_t object_id;
  MoqtPriority publisher_priority;
  MoqtObjectStatus object_status;
  MoqtForwardingPreference forwarding_preference;
  std::optional<uint64_t> payload_length;
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

  // If present, indicates that OBJECT_ACK messages will be sent in response to
  // the objects on the stream. The actual value is informational, and it
  // communicates how many frames the subscriber is willing to buffer, in
  // microseconds.
  std::optional<quic::QuicTimeDelta> object_ack_window;
};

struct QUICHE_EXPORT MoqtSubscribe {
  uint64_t subscribe_id;
  uint64_t track_alias;
  std::string track_namespace;
  std::string track_name;
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
};

enum class QUICHE_EXPORT SubscribeErrorCode : uint64_t {
  kInternalError = 0x0,
  kInvalidRange = 0x1,
  kRetryTrackAlias = 0x2,
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
  std::optional<std::string> authorization_info;
};

struct QUICHE_EXPORT MoqtAnnounce {
  std::string track_namespace;
  std::optional<std::string> authorization_info;
};

struct QUICHE_EXPORT MoqtAnnounceOk {
  std::string track_namespace;
};

struct QUICHE_EXPORT MoqtAnnounceError {
  std::string track_namespace;
  MoqtAnnounceErrorCode error_code;
  std::string reason_phrase;
};

struct QUICHE_EXPORT MoqtUnannounce {
  std::string track_namespace;
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
  std::string track_namespace;
  std::string track_name;
  MoqtTrackStatusCode status_code;
  uint64_t last_group;
  uint64_t last_object;
};

struct QUICHE_EXPORT MoqtAnnounceCancel {
  std::string track_namespace;
};

struct QUICHE_EXPORT MoqtTrackStatusRequest {
  std::string track_namespace;
  std::string track_name;
};

struct QUICHE_EXPORT MoqtGoAway {
  std::string new_session_uri;
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

std::string MoqtForwardingPreferenceToString(
    MoqtForwardingPreference preference);

MoqtForwardingPreference GetForwardingPreference(MoqtMessageType type);

MoqtMessageType GetMessageTypeForForwardingPreference(
    MoqtForwardingPreference preference);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
