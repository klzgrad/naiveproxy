// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Structured data for message types in draft-ietf-moq-transport-01.

#ifndef QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
#define QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace moqt {

enum class MoqtVersion : uint64_t {
  kDraft01 = 0xff000001,
  kUnrecognizedVersionForTests = 0xfe0000ff,
};

struct QUICHE_EXPORT MoqtSessionParameters {
  // TODO: support multiple versions.
  MoqtVersion version;
  quic::Perspective perspective;
  bool using_webtrans;
  std::string path;
};

// The maximum length of a message, excluding any OBJECT payload. This prevents
// DoS attack via forcing the parser to buffer a large message (OBJECT payloads
// are not buffered by the parser).
inline constexpr size_t kMaxMessageHeaderSize = 4096;

enum class QUICHE_EXPORT MoqtMessageType : uint64_t {
  kObject = 0x00,
  kSetup = 0x01,
  kSubscribeRequest = 0x03,
  kSubscribeOk = 0x04,
  kSubscribeError = 0x05,
  kAnnounce = 0x06,
  kAnnounceOk = 0x7,
  kAnnounceError = 0x08,
  kUnannounce = 0x09,
  kUnsubscribe = 0x0a,
  kGoAway = 0x10,
};

enum class QUICHE_EXPORT MoqtRole : uint64_t {
  kIngestion = 0x1,
  kDelivery = 0x2,
  kBoth = 0x3,
};

enum class QUICHE_EXPORT MoqtSetupParameter : uint64_t {
  kRole = 0x0,
  kPath = 0x1,
};

enum class QUICHE_EXPORT MoqtTrackRequestParameter : uint64_t {
  kGroupSequence = 0x0,
  kObjectSequence = 0x1,
  kAuthorizationInfo = 0x2,
};

struct QUICHE_EXPORT MoqtSetup {
  std::vector<MoqtVersion> supported_versions;
  absl::optional<MoqtRole> role;
  absl::optional<absl::string_view> path;
};

struct QUICHE_EXPORT MoqtObject {
  uint64_t track_id;
  uint64_t group_sequence;
  uint64_t object_sequence;
  uint64_t object_send_order;
  // Message also includes the object payload.
};

struct QUICHE_EXPORT MoqtSubscribeRequest {
  absl::string_view full_track_name;
  absl::optional<uint64_t> group_sequence;
  absl::optional<uint64_t> object_sequence;
  absl::optional<absl::string_view> authorization_info;
};

struct QUICHE_EXPORT MoqtSubscribeOk {
  absl::string_view full_track_name;
  uint64_t track_id;
  // The message uses ms, but expires is in us.
  quic::QuicTimeDelta expires = quic::QuicTimeDelta::FromMilliseconds(0);
};

struct QUICHE_EXPORT MoqtSubscribeError {
  absl::string_view full_track_name;
  uint64_t error_code;
  absl::string_view reason_phrase;
};

struct QUICHE_EXPORT MoqtUnsubscribe {
  absl::string_view full_track_name;
};

struct QUICHE_EXPORT MoqtAnnounce {
  absl::string_view track_namespace;
  absl::optional<absl::string_view> authorization_info;
};

struct QUICHE_EXPORT MoqtAnnounceOk {
  absl::string_view track_namespace;
};

struct QUICHE_EXPORT MoqtAnnounceError {
  absl::string_view track_namespace;
  uint64_t error_code;
  absl::string_view reason_phrase;
};

struct QUICHE_EXPORT MoqtUnannounce {
  absl::string_view track_namespace;
};

struct QUICHE_EXPORT MoqtGoAway {};

std::string MoqtMessageTypeToString(MoqtMessageType message_type);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_MESSAGES_H_
