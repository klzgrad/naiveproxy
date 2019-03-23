// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_FLAGS_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_FLAGS_H_

#include <vector>

#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/impl/quic_flags_impl.h"

// Define a command-line flag that can be automatically set via
// QuicParseCommandLineFlags().
#define DEFINE_QUIC_COMMAND_LINE_FLAG(type, name, default_value, help) \
  DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, name, default_value, help)

#define GetQuicReloadableFlag(flag) GetQuicReloadableFlagImpl(flag)
#define SetQuicReloadableFlag(flag, value) \
  SetQuicReloadableFlagImpl(flag, value)
#define GetQuicRestartFlag(flag) GetQuicRestartFlagImpl(flag)
#define SetQuicRestartFlag(flag, value) SetQuicRestartFlagImpl(flag, value)
#define GetQuicFlag(flag) GetQuicFlagImpl(flag)
#define SetQuicFlag(flag, value) SetQuicFlagImpl(flag, value)

namespace quic {

// Parses command-line flags, setting flag variables defined using
// DEFINE_QUIC_COMMAND_LINE_FLAG if they appear in the command line, and
// returning a list of any non-flag arguments specified in the command line. If
// the command line specifies '-h' or '--help', prints a usage message with flag
// descriptions to stdout and exits with status 0. If a flag has an unparsable
// value, writes an error message to stderr and exits with status 1.
inline std::vector<QuicString> QuicParseCommandLineFlags(
    const char* usage,
    int argc,
    const char* const* argv) {
  return QuicParseCommandLineFlagsImpl(usage, argc, argv);
}

// Prints a usage message with flag descriptions to stdout.
inline void QuicPrintCommandLineFlagHelp(const char* usage) {
  QuicPrintCommandLineFlagHelpImpl(usage);
}

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_FLAGS_H_
