// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DEVICE_BOUND_SESSIONS_SESSION_PARAMS_H_
#define NET_DEVICE_BOUND_SESSIONS_SESSION_PARAMS_H_

#include <string>
#include <variant>
#include <vector>

#include "net/base/net_export.h"

namespace net::device_bound_sessions {

// Struct to contain the parameters from the session instruction JSON.
// https://github.com/WICG/dbsc/blob/main/README.md#session-registration-instructions-json
// This is sent on session creation and session refresh
struct NET_EXPORT SessionParams final {
  // Scope section of session instructions.
  struct NET_EXPORT Scope {
    // Specification section of the session scope instructions.
    struct NET_EXPORT Specification {
      enum class Type { kExclude, kInclude };
      bool operator==(const Specification&) const = default;
      Type type;
      std::string domain;
      std::string path;
    };

    // Defaults to false if not in the params
    bool include_site = false;
    std::vector<Specification> specifications;
    std::string origin;

    Scope();
    Scope(Scope&& other) noexcept;
    Scope& operator=(Scope&& other) noexcept;
    ~Scope();
  };

  // Credential section of the session instruction.
  struct NET_EXPORT Credential {
    bool operator==(const Credential&) const = default;
    std::string name;
    std::string attributes;
  };

  SessionParams(std::string id,
                std::string refresh_url,
                Scope scope,
                std::vector<Credential> creds);
  SessionParams(SessionParams&& other) noexcept;
  SessionParams& operator=(SessionParams&& other) noexcept;

  ~SessionParams();

  std::string session_id;
  std::string refresh_url;
  Scope scope;
  std::vector<Credential> credentials;
};

struct SessionTerminationParams {
  std::string session_id;
};

using ParsedSessionParams =
    std::variant<SessionParams, SessionTerminationParams>;

}  // namespace net::device_bound_sessions

#endif  // NET_DEVICE_BOUND_SESSIONS_SESSION_PARAMS_H_
