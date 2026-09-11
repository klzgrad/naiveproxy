// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DEVICE_BOUND_SESSIONS_SESSION_KEY_H_
#define NET_DEVICE_BOUND_SESSIONS_SESSION_KEY_H_

#include "base/types/strong_alias.h"
#include "net/base/schemeful_site.h"

namespace net::device_bound_sessions {

// Unique identifier for a `Session`.
// LINT.IfChange
struct NET_EXPORT SessionKey {
  using Id = base::StrongAlias<class IdTag, std::string>;
  SchemefulSite site;
  Id id;

  friend bool operator==(const SessionKey&, const SessionKey&) = default;
  friend auto operator<=>(const SessionKey&, const SessionKey&) = default;

  template <typename H>
  friend H AbslHashValue(H h, const SessionKey& key) {
    return H::combine(std::move(h), key.site, key.id);
  }
};
// LINT.ThenChange(//services/network/public/mojom/device_bound_sessions.mojom)

}  // namespace net::device_bound_sessions

#endif  // NET_DEVICE_BOUND_SESSIONS_SESSION_KEY_H_
