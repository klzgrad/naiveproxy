// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche_platform_impl/quiche_command_line_flags_impl.h"

#include <stddef.h>

#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/initialize.h"
#include "absl/strings/string_view.h"

namespace quiche {

static void SetUsage(absl::string_view usage) {
  static bool usage_set = false;
  if (!usage_set) {
    absl::SetProgramUsageMessage(usage);
    usage_set = true;
  }
}

std::vector<std::string> QuicheParseCommandLineFlagsImpl(
    const char* usage, int argc, const char* const* argv, bool /*parse_only*/) {
  SetUsage(usage);
  std::vector<char*> parsed =
      absl::ParseCommandLine(argc, const_cast<char**>(argv));
  absl::InitializeLog();
  std::vector<std::string> result;
  result.reserve(parsed.size());
  // Remove the first argument, which is the name of the binary.
  for (size_t i = 1; i < parsed.size(); i++) {
    result.push_back(std::string(parsed[i]));
  }
  return result;
}

void QuichePrintCommandLineFlagHelpImpl(const char* usage) {
  SetUsage(usage);
  std::cerr << absl::ProgramUsageMessage() << std::endl;
}

}  // namespace quiche
