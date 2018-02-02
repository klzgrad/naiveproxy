// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_ESCAPE_H_
#define TOOLS_GN_ESCAPE_H_

#include <iosfwd>

#include "base/strings/string_piece.h"

enum EscapingMode {
  // No escaping.
  ESCAPE_NONE,

  // Ninja string escaping.
  ESCAPE_NINJA,

  // For writing commands to ninja files. This assumes the output is "one
  // thing" like a filename, so will escape or quote spaces as necessary for
  // both Ninja and the shell to keep that thing together.
  ESCAPE_NINJA_COMMAND,

  // For writing preformatted shell commands to Ninja files. This assumes the
  // output already has the proper quoting and may include special shell
  // shell characters which we want to pass to the shell (like when writing
  // tool commands). Only Ninja "$" are escaped.
  ESCAPE_NINJA_PREFORMATTED_COMMAND,
};

enum EscapingPlatform {
  // Do escaping for the current platform.
  ESCAPE_PLATFORM_CURRENT,

  // Force escaping for the given platform.
  ESCAPE_PLATFORM_POSIX,
  ESCAPE_PLATFORM_WIN,
};

struct EscapeOptions {
  EscapeOptions()
      : mode(ESCAPE_NONE),
        platform(ESCAPE_PLATFORM_CURRENT),
        inhibit_quoting(false) {
  }

  EscapingMode mode;

  // Controls how "fork" escaping is done. You will generally want to keep the
  // default "current" platform.
  EscapingPlatform platform;

  // When the escaping mode is ESCAPE_SHELL, the escaper will normally put
  // quotes around things with spaces. If this value is set to true, we'll
  // disable the quoting feature and just add the spaces.
  //
  // This mode is for when quoting is done at some higher-level. Defaults to
  // false. Note that Windows has strange behavior where the meaning of the
  // backslashes changes according to if it is followed by a quote. The
  // escaping rules assume that a double-quote will be appended to the result.
  bool inhibit_quoting;
};

// Escapes the given input, returnining the result.
//
// If needed_quoting is non-null, whether the string was or should have been
// (if inhibit_quoting was set) quoted will be written to it. This value should
// be initialized to false by the caller and will be written to only if it's
// true (the common use-case is for chaining calls).
std::string EscapeString(const base::StringPiece& str,
                         const EscapeOptions& options,
                         bool* needed_quoting);

// Same as EscapeString but writes the results to the given stream, saving a
// copy.
void EscapeStringToStream(std::ostream& out,
                          const base::StringPiece& str,
                          const EscapeOptions& options);

#endif  // TOOLS_GN_ESCAPE_H_
