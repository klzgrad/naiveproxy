// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_COMMAND_LINE_FLAGS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_COMMAND_LINE_FLAGS_IMPL_H_

#include <string>
#include <vector>

#include "absl/flags/flag.h"

#define DEFINE_QUICHE_COMMAND_LINE_FLAG_IMPL(type, name, default_value, help) \
  ABSL_FLAG(type, name, default_value, help)

namespace quiche {

template <typename T>
T GetQuicheCommandLineFlag(const absl::Flag<T>& flag) {
  return absl::GetFlag(flag);
}

std::vector<std::string> QuicheParseCommandLineFlagsImpl(
    const char* usage, int argc, const char* const* argv,
    bool parse_only = false);

void QuichePrintCommandLineFlagHelpImpl(const char* usage);

}  // namespace quiche

template <typename T>
T GetQuicheFlagImplImpl(const absl::Flag<T>& flag) {
  return absl::GetFlag(flag);
}

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_COMMAND_LINE_FLAGS_IMPL_H_
