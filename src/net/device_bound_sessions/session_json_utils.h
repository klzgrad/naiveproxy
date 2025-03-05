// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DEVICE_BOUND_SESSIONS_SESSION_JSON_UTILS_H_
#define NET_DEVICE_BOUND_SESSIONS_SESSION_JSON_UTILS_H_

#include <optional>
#include <vector>

#include "net/device_bound_sessions/session_params.h"

namespace net::device_bound_sessions {

// Utilities for parsing the JSON session specification
// https://github.com/WICG/dbsc/blob/main/README.md#session-registration-instructions-json

// Parse the full JSON as a string. Returns:
// - A SessionParams describing the session to be created on success
// - A SessionTerminationParams describing the session to be terminated
//   when the JSON contains "continue": false
// - std::nullopt if parsing fails (e.g. invalid JSON)
std::optional<ParsedSessionParams> ParseSessionInstructionJson(
    std::string_view response_json);

}  // namespace net::device_bound_sessions

#endif  // NET_DEVICE_BOUND_SESSIONS_SESSION_JSON_UTILS_H_
