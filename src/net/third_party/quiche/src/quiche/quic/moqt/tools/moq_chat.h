// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQ_CHAT_H
#define QUICHE_QUIC_MOQT_TOOLS_MOQ_CHAT_H

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"

namespace moqt {

// This class encodes all the syntax in moq-chat strings: paths, full track
// names, and catalog entries.
class MoqChatStrings {
 public:
  explicit MoqChatStrings(absl::string_view chat_id) : chat_id_(chat_id) {}

  static constexpr absl::string_view kBasePath = "moq-chat";
  static constexpr absl::string_view kParticipantPath = "participant";
  static constexpr absl::string_view kCatalogPath = "catalog";
  static constexpr absl::string_view kCatalogHeader = "version=1\n";

  // Verifies that the WebTransport path matches the spec.
  bool IsValidPath(absl::string_view path) const {
    return path == absl::StrCat("/", kBasePath);
  }

  // Returns "" if the track namespace is not a participant track.
  std::string GetUsernameFromTrackNamespace(
      absl::string_view track_namespace) const {
    std::vector<absl::string_view> elements =
        absl::StrSplit(track_namespace, '/');
    if (elements.size() != 4 || elements[0] != kBasePath ||
        elements[1] != chat_id_ || elements[2] != kParticipantPath) {
      return "";
    }
    return std::string(elements[3]);
  }

  // Returns "" if the full track name is not a participant track.
  std::string GetUsernameFromFullTrackName(
      FullTrackName full_track_name) const {
    // Check the full path
    if (!full_track_name.track_name.empty()) {
      return "";
    }
    return GetUsernameFromTrackNamespace(full_track_name.track_namespace);
  }

  FullTrackName GetFullTrackNameFromUsername(absl::string_view username) const {
    return FullTrackName{absl::StrCat(kBasePath, "/", chat_id_, "/",
                                      kParticipantPath, "/", username),
                         ""};
  }

  FullTrackName GetCatalogName() const {
    return FullTrackName{absl::StrCat(kBasePath, "/", chat_id_),
                         absl::StrCat("/", kCatalogPath)};
  }

 private:
  const std::string chat_id_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQ_CHAT_H
