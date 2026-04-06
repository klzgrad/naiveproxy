// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_FILE_UTILS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_FILE_UTILS_IMPL_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace quiche {

std::string JoinPathImpl(absl::string_view a, absl::string_view b);

std::optional<std::string> ReadFileContentsImpl(absl::string_view file);

bool EnumerateDirectoryImpl(absl::string_view path,
                            std::vector<std::string>& directories,
                            std::vector<std::string>& files);

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_FILE_UTILS_IMPL_H_
