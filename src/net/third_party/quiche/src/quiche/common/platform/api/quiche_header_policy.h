// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_HEADER_POLICY_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_HEADER_POLICY_H_

#include "quiche_platform_impl/quiche_header_policy_impl.h"

#include "absl/strings/string_view.h"

namespace quiche {

// Invoke some platform-specific action based on header key.
inline void QuicheHandleHeaderPolicy(absl::string_view key) {
  QuicheHandleHeaderPolicyImpl(key);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_HEADER_POLICY_H_
