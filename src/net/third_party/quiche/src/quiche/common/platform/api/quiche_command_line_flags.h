// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_COMMAND_LINE_FLAGS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_COMMAND_LINE_FLAGS_H_

#include <string>
#include <vector>

#include "quiche_platform_impl/quiche_command_line_flags_impl.h"

// Define a command-line flag that can be automatically set via
// QuicheParseCommandLineFlags().
#define DEFINE_QUICHE_COMMAND_LINE_FLAG(type, name, default_value, help) \
  DEFINE_QUICHE_COMMAND_LINE_FLAG_IMPL(type, name, default_value, help)

namespace quiche {

// Parses command-line flags, setting flag variables defined using
// DEFINE_QUICHE_COMMAND_LINE_FLAG if they appear in the command line, and
// returning a list of any non-flag arguments specified in the command line. If
// the command line specifies '-h' or '--help', prints a usage message with flag
// descriptions to stdout and exits with status 0. If a flag has an unparsable
// value, writes an error message to stderr and exits with status 1.
inline std::vector<std::string> QuicheParseCommandLineFlags(
    const char* usage, int argc, const char* const* argv) {
  return QuicheParseCommandLineFlagsImpl(usage, argc, argv);
}

// Prints a usage message with flag descriptions to stdout.
inline void QuichePrintCommandLineFlagHelp(const char* usage) {
  QuichePrintCommandLineFlagHelpImpl(usage);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_COMMAND_LINE_FLAGS_H_
