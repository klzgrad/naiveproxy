// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header contains basic filesystem functions for use in unit tests and CLI
// tools.  Note that those are not 100% suitable for production use, as in, they
// might be prone to race conditions and not always handle non-ASCII filenames
// correctly.
#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_FILE_UTILS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_FILE_UTILS_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace quiche {

// Join two paths in a platform-specific way.  Returns |a| if |b| is empty, and
// vice versa.
std::string JoinPath(absl::string_view a, absl::string_view b);

// Reads the entire file into the memory.
absl::optional<std::string> ReadFileContents(absl::string_view file);

// Lists all files and directories in the directory specified by |path|. Returns
// true on success, false on failure.
bool EnumerateDirectory(absl::string_view path,
                        std::vector<std::string>& directories,
                        std::vector<std::string>& files);

// Recursively enumerates all of the files in the directory and all of the
// internal subdirectories.  Has a fairly small recursion limit.
bool EnumerateDirectoryRecursively(absl::string_view path,
                                   std::vector<std::string>& files);

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_FILE_UTILS_H_
