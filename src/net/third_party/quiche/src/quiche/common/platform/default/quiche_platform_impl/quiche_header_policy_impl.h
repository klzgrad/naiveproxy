// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_HEADER_POLICY_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_HEADER_POLICY_IMPL_H_

#include "absl/strings/string_view.h"

namespace quiche {

inline void QuicheHandleHeaderPolicyImpl(absl::string_view /*key*/) {}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_HEADER_POLICY_IMPL_H_
