// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_FLAGS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_FLAGS_H_

#include <string>
#include <vector>

#include "net/quic/platform/impl/quic_flags_impl.h"

#include "common/platform/api/quiche_flags.h"

#define GetQuicReloadableFlag(flag) GetQuicheReloadableFlag(quic, flag)
#define SetQuicReloadableFlag(flag, value) \
  SetQuicheReloadableFlag(quic, flag, value)
#define GetQuicRestartFlag(flag) GetQuicheRestartFlag(quic, flag)
#define SetQuicRestartFlag(flag, value) SetQuicheRestartFlag(quic, flag, value)
#define GetQuicFlag(flag) GetQuicheFlag(flag)
#define SetQuicFlag(flag, value) SetQuicheFlag(flag, value)

// Define a command-line flag that can be automatically set via
// QuicParseCommandLineFlags().
#define DEFINE_QUIC_COMMAND_LINE_FLAG(type, name, default_value, help) \
  DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, name, default_value, help)

namespace quic {

// Parses command-line flags, setting flag variables defined using
// DEFINE_QUIC_COMMAND_LINE_FLAG if they appear in the command line, and
// returning a list of any non-flag arguments specified in the command line. If
// the command line specifies '-h' or '--help', prints a usage message with flag
// descriptions to stdout and exits with status 0. If a flag has an unparsable
// value, writes an error message to stderr and exits with status 1.
inline std::vector<std::string> QuicParseCommandLineFlags(
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

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_FLAGS_H_
