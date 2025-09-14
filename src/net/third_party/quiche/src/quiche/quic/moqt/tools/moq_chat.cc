// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/moq_chat.h"

#include <optional>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace moqt::moq_chat {

bool IsValidPath(absl::string_view path) { return path == kWebtransPath; }

bool IsValidNamespace(const TrackNamespace& track_namespace) {
  return track_namespace.number_of_elements() == kFullPathLength - 1 &&
         track_namespace.tuple()[0] == kBasePath;
}

bool IsValidChatNamespace(const TrackNamespace& track_namespace) {
  return track_namespace.tuple().size() == 2 &&
         track_namespace.tuple()[0] == kBasePath;
}

FullTrackName ConstructTrackName(absl::string_view chat_id,
                                 absl::string_view username,
                                 absl::string_view device_id) {
  return FullTrackName(
      TrackNamespace({kBasePath, chat_id, username, device_id,
                      absl::StrCat(ToUnixSeconds(::absl::Now()))}),
      kNameField);
}

std::optional<FullTrackName> ConstructTrackNameFromNamespace(
    const TrackNamespace& track_namespace, absl::string_view chat_id) {
  if (track_namespace.number_of_elements() != kFullPathLength - 1) {
    return std::nullopt;
  }
  if (track_namespace.tuple()[0] != kBasePath ||
      track_namespace.tuple()[1] != chat_id) {
    return std::nullopt;
  }
  return FullTrackName(track_namespace, kNameField);
}

absl::string_view GetUsername(const TrackNamespace& track_namespace) {
  QUICHE_DCHECK(track_namespace.number_of_elements() > 2);
  return track_namespace.tuple()[2];
}
absl::string_view GetUsername(const FullTrackName& track_name) {
  return GetUsername(track_name.track_namespace());
}

absl::string_view GetChatId(const TrackNamespace& track_namespace) {
  QUICHE_DCHECK(track_namespace.number_of_elements() > 1);
  return track_namespace.tuple()[1];
}
absl::string_view GetChatId(const FullTrackName& track_name) {
  return GetChatId(track_name.track_namespace());
}

const TrackNamespace& GetUserNamespace(const FullTrackName& track_name) {
  return track_name.track_namespace();
}

TrackNamespace GetChatNamespace(const TrackNamespace& track_namespace) {
  return TrackNamespace(
      {track_namespace.tuple()[0], track_namespace.tuple()[1]});
}
TrackNamespace GetChatNamespace(const FullTrackName& track_name) {
  return GetChatNamespace(track_name.track_namespace());
}

}  // namespace moqt::moq_chat
