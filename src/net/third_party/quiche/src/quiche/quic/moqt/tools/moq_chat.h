// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQ_CHAT_H
#define QUICHE_QUIC_MOQT_TOOLS_MOQ_CHAT_H

#include <cstddef>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"

// Utilities for manipulating moq-chat paths, names, and namespaces.

namespace moqt {

namespace moq_chat {

constexpr absl::string_view kWebtransPath = "/moq-relay";
// The order of fields is "moq-chat", chat-id, username, device-id, timestamp,
// "chat".
// The number of tiers in a full track name.
constexpr size_t kFullPathLength = 6;
// The first element in the track namespace or name.
constexpr absl::string_view kBasePath = "moq-chat";
// The last element in the track name.
constexpr absl::string_view kNameField = "chat";

// Verifies that the WebTransport path matches the spec.
bool IsValidPath(absl::string_view path);

bool IsValidTrackNamespace(const FullTrackName& track_namespace);
bool IsValidChatNamespace(const FullTrackName& track_namespace);

// Given a chat-id and username, returns a full track name for moq-chat.
FullTrackName ConstructTrackName(absl::string_view chat_id,
                                 absl::string_view username,
                                 absl::string_view device_id);

// constructs a full track name based on the track_namespace. If the namespace
// is syntactically incorrect, or does not match the expected value of
// |chat-id|, returns nullopt
std::optional<FullTrackName> ConstructTrackNameFromNamespace(
    const FullTrackName& track_namespace, absl::string_view chat_id);

// Strips "chat" from the end of |track_name| to use in ANNOUNCE.
FullTrackName GetUserNamespace(const FullTrackName& track_name);

// Returns {"moq-chat", chat-id}, useful for SUBSCRIBE_ANNOUNCES.
FullTrackName GetChatNamespace(const FullTrackName& track_name);

absl::string_view GetUsername(const FullTrackName& track_name);

absl::string_view GetChatId(const FullTrackName& track_name);

}  // namespace moq_chat

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQ_CHAT_H
