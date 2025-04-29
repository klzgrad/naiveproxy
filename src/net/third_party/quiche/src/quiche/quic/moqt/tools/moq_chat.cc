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

bool IsValidNamespace(const FullTrackName& track_namespace) {
  return track_namespace.tuple().size() == kFullPathLength - 1 &&
         track_namespace.tuple()[0] == kBasePath;
}

bool IsValidChatNamespace(const FullTrackName& track_namespace) {
  return track_namespace.tuple().size() == 2 &&
         track_namespace.tuple()[0] == kBasePath;
}

FullTrackName ConstructTrackName(absl::string_view chat_id,
                                 absl::string_view username,
                                 absl::string_view device_id) {
  return FullTrackName{kBasePath,
                       chat_id,
                       username,
                       device_id,
                       absl::StrCat(ToUnixSeconds(::absl::Now())),
                       kNameField};
}

std::optional<FullTrackName> ConstructTrackNameFromNamespace(
    const FullTrackName& track_namespace, absl::string_view chat_id) {
  if (track_namespace.tuple().size() != kFullPathLength - 1) {
    return std::nullopt;
  }
  if (track_namespace.tuple()[0] != kBasePath ||
      track_namespace.tuple()[1] != chat_id) {
    return std::nullopt;
  }
  FullTrackName track_name = track_namespace;
  track_name.AddElement(kNameField);
  return track_name;
}

absl::string_view GetUsername(const FullTrackName& track_name) {
  QUICHE_DCHECK(track_name.tuple().size() > 2);
  return track_name.tuple()[2];
}

absl::string_view GetChatId(const FullTrackName& track_name) {
  QUICHE_DCHECK(track_name.tuple().size() > 1);
  return track_name.tuple()[1];
}

FullTrackName GetUserNamespace(const FullTrackName& track_name) {
  QUICHE_DCHECK(track_name.tuple().size() == kFullPathLength);
  FullTrackName track_namespace = track_name;
  track_namespace.NameToNamespace();
  return track_namespace;
}

FullTrackName GetChatNamespace(const FullTrackName& track_name) {
  return FullTrackName{track_name.tuple()[0], track_name.tuple()[1]};
}

}  // namespace moqt::moq_chat
