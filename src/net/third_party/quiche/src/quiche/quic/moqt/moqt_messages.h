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

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace moqt {

inline constexpr quic::ParsedQuicVersionVector GetMoqtSupportedQuicVersions() {
  return quic::ParsedQuicVersionVector{quic::ParsedQuicVersion::RFCv1()};
}

enum class MoqtVersion : uint64_t {
  kDraft03 = 0xff000003,
  kUnrecognizedVersionForTests = 0xfe0000ff,
};

struct QUICHE_EXPORT MoqtSessionParameters {
  // TODO: support multiple versions.
  // TODO: support roles other than PubSub.
  MoqtVersion version;
  quic::Perspective perspective;
  bool using_webtrans;
  std::string path;
  bool deliver_partial_objects;
};

// The maximum length of a message, excluding any OBJECT payload. This prevents
// DoS attack via forcing the parser to buffer a large message (OBJECT payloads
// are not buffered by the parser).
inline constexpr size_t kMaxMessageHeaderSize = 2048;

enum class QUICHE_EXPORT MoqtMessageType : uint64_t {
  kObjectStream = 0x00,
  kObjectDatagram = 0x01,
  kSubscribe = 0x03,
  kSubscribeOk = 0x04,
  kSubscribeError = 0x05,
  kAnnounce = 0x06,
  kAnnounceOk = 0x7,
  kAnnounceError = 0x08,
  kUnannounce = 0x09,
  kUnsubscribe = 0x0a,
  kSubscribeDone = 0x0b,
  kGoAway = 0x10,
  kClientSetup = 0x40,
  kServerSetup = 0x41,
  kStreamHeaderTrack = 0x50,
  kStreamHeaderGroup = 0x51,
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

enum class QUICHE_EXPORT MoqtRole : uint64_t {
  kPublisher = 0x1,
  kSubscriber = 0x2,
  kPubSub = 0x3,
  kRoleMax = 0x3,
};

enum class QUICHE_EXPORT MoqtSetupParameter : uint64_t {
  kRole = 0x0,
  kPath = 0x1,
};

enum class QUICHE_EXPORT MoqtTrackRequestParameter : uint64_t {
  kAuthorizationInfo = 0x2,
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
  FullTrackName& operator=(FullTrackName other) {
    track_namespace = other.track_namespace;
    track_name = other.track_name;
    return *this;
  }
  template <typename H>
  friend H AbslHashValue(H h, const FullTrackName& m);
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
  FullSequence& operator=(FullSequence other) {
    group = other.group;
    object = other.object;
    return *this;
  }
  template <typename H>
  friend H AbslHashValue(H h, const FullSequence& m);
};

template <typename H>
H AbslHashValue(H h, const FullSequence& m) {
  return H::combine(std::move(h), m.group, m.object);
}

struct QUICHE_EXPORT MoqtClientSetup {
  std::vector<MoqtVersion> supported_versions;
  std::optional<MoqtRole> role;
  std::optional<std::string> path;
};

struct QUICHE_EXPORT MoqtServerSetup {
  MoqtVersion selected_version;
  std::optional<MoqtRole> role;
};

// These codes do not appear on the wire.
enum class QUICHE_EXPORT MoqtForwardingPreference : uint8_t {
  kTrack = 0x0,
  kGroup = 0x1,
  kObject = 0x2,
  kDatagram = 0x3,
};

// The data contained in every Object message, although the message type
// implies some of the values. |payload_length| has no value if the length
// is unknown (because it runs to the end of the stream.)
struct QUICHE_EXPORT MoqtObject {
  uint64_t subscribe_id;
  uint64_t track_alias;
  uint64_t group_id;
  uint64_t object_id;
  uint64_t object_send_order;
  MoqtForwardingPreference forwarding_preference;
  std::optional<uint64_t> payload_length;
};

enum class QUICHE_EXPORT MoqtSubscribeLocationMode : uint64_t {
  kNone = 0x0,
  kAbsolute = 0x1,
  kRelativePrevious = 0x2,
  kRelativeNext = 0x3,
};

// kNone: std::optional<MoqtSubscribeLocation> is nullopt.
// kAbsolute: absolute = true
// kRelativePrevious: absolute is false; relative_value is negative
// kRelativeNext: absolute is true; relative_value is positive
struct QUICHE_EXPORT MoqtSubscribeLocation {
  MoqtSubscribeLocation(bool is_absolute, uint64_t abs)
      : absolute(is_absolute), absolute_value(abs) {}
  MoqtSubscribeLocation(bool is_absolute, int64_t rel)
      : absolute(is_absolute), relative_value(rel) {}
  bool absolute;
  union {
    uint64_t absolute_value;
    int64_t relative_value;
  };
  bool operator==(const MoqtSubscribeLocation& other) const {
    return absolute == other.absolute &&
           ((absolute && absolute_value == other.absolute_value) ||
            (!absolute && relative_value == other.relative_value));
  }
};

inline MoqtSubscribeLocationMode GetModeForSubscribeLocation(
    const std::optional<MoqtSubscribeLocation>& location) {
  if (!location.has_value()) {
    return MoqtSubscribeLocationMode::kNone;
  }
  if (location->absolute) {
    return MoqtSubscribeLocationMode::kAbsolute;
  }
  return location->relative_value >= 0
             ? MoqtSubscribeLocationMode::kRelativeNext
             : MoqtSubscribeLocationMode::kRelativePrevious;
}

struct QUICHE_EXPORT MoqtSubscribe {
  uint64_t subscribe_id;
  uint64_t track_alias;
  std::string track_namespace;
  std::string track_name;
  // If the mode is kNone, the these are std::nullopt.
  std::optional<MoqtSubscribeLocation> start_group;
  std::optional<MoqtSubscribeLocation> start_object;
  std::optional<MoqtSubscribeLocation> end_group;
  std::optional<MoqtSubscribeLocation> end_object;
  std::optional<std::string> authorization_info;
};

struct QUICHE_EXPORT MoqtSubscribeOk {
  uint64_t subscribe_id;
  // The message uses ms, but expires is in us.
  quic::QuicTimeDelta expires = quic::QuicTimeDelta::FromMilliseconds(0);
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
  uint64_t status_code;
  std::string reason_phrase;
  std::optional<FullSequence> final_id;
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

struct QUICHE_EXPORT MoqtGoAway {
  std::string new_session_uri;
};

std::string MoqtMessageTypeToString(MoqtMessageType message_type);

std::string MoqtForwardingPreferenceToString(
    MoqtForwardingPreference preference);

MoqtForwardingPreference GetForwardingPreference(MoqtMessageType type);

MoqtMessageType GetMessageTypeForForwardingPreference(
    MoqtForwardingPreference preference);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
