// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/escape.h"

#include <stddef.h>

#include "base/logging.h"
#include "build/build_config.h"

namespace {

// A "1" in this lookup table means that char is valid in the Posix shell.
const char kShellValid[0x80] = {
// 00-1f: all are invalid
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
// ' ' !  "  #  $  %  &  '  (  )  *  +  ,  -  .  /
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
//  0  1  2  3  4  5  6  7  8  9  :  ;  <  =  >  ?
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0,
//  @  A  B  C  D  E  F  G  H  I  J  K  L  M  N  O
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//  P  Q  R  S  T  U  V  W  X  Y  Z  [  \  ]  ^  _
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
//  `  a  b  c  d  e  f  g  h  i  j  k  l  m  n  o
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//  p  q  r  s  t  u  v  w  x  y  z  {  |  }  ~
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };

// Append one character to the given string, escaping it for Ninja.
//
// Ninja's escaping rules are very simple. We always escape colons even
// though they're OK in many places, in case the resulting string is used on
// the left-hand-side of a rule.
inline void NinjaEscapeChar(char ch, std::string* dest) {
  if (ch == '$' || ch == ' ' || ch == ':')
    dest->push_back('$');
  dest->push_back(ch);
}

void EscapeStringToString_Ninja(const base::StringPiece& str,
                                const EscapeOptions& options,
                                std::string* dest,
                                bool* needed_quoting) {
  for (const auto& elem : str)
    NinjaEscapeChar(elem, dest);
}

void EscapeStringToString_NinjaPreformatted(const base::StringPiece& str,
                                            std::string* dest) {
  // Only Ninja-escape $.
  for (const auto& elem : str) {
    if (elem == '$')
      dest->push_back('$');
    dest->push_back(elem);
  }
}

// Escape for CommandLineToArgvW and additionally escape Ninja characters.
//
// The basic algorithm is if the string doesn't contain any parse-affecting
// characters, don't do anything (other than the Ninja processing). If it does,
// quote the string, and backslash-escape all quotes and backslashes.
// See:
//   http://blogs.msdn.com/b/twistylittlepassagesallalike/archive/2011/04/23/everyone-quotes-arguments-the-wrong-way.aspx
//   http://blogs.msdn.com/b/oldnewthing/archive/2010/09/17/10063629.aspx
void EscapeStringToString_WindowsNinjaFork(const base::StringPiece& str,
                                           const EscapeOptions& options,
                                           std::string* dest,
                                           bool* needed_quoting) {
  // We assume we don't have any whitespace chars that aren't spaces.
  DCHECK(str.find_first_of("\r\n\v\t") == std::string::npos);

  if (str.find_first_of(" \"") == std::string::npos) {
    // Simple case, don't quote.
    EscapeStringToString_Ninja(str, options, dest, needed_quoting);
  } else {
    if (!options.inhibit_quoting)
      dest->push_back('"');

    for (size_t i = 0; i < str.size(); i++) {
      // Count backslashes in case they're followed by a quote.
      size_t backslash_count = 0;
      while (i < str.size() && str[i] == '\\') {
        i++;
        backslash_count++;
      }
      if (i == str.size()) {
        // Backslashes at end of string. Backslash-escape all of them since
        // they'll be followed by a quote.
        dest->append(backslash_count * 2, '\\');
      } else if (str[i] == '"') {
        // 0 or more backslashes followed by a quote. Backslash-escape the
        // backslashes, then backslash-escape the quote.
        dest->append(backslash_count * 2 + 1, '\\');
        dest->push_back('"');
      } else {
        // Non-special Windows character, just escape for Ninja. Also, add any
        // backslashes we read previously, these are literals.
        dest->append(backslash_count, '\\');
        NinjaEscapeChar(str[i], dest);
      }
    }

    if (!options.inhibit_quoting)
      dest->push_back('"');
    if (needed_quoting)
      *needed_quoting = true;
  }
}

void EscapeStringToString_PosixNinjaFork(const base::StringPiece& str,
                                         const EscapeOptions& options,
                                         std::string* dest,
                                         bool* needed_quoting) {
  for (const auto& elem : str) {
    if (elem == '$' || elem == ' ') {
      // Space and $ are special to both Ninja and the shell. '$' escape for
      // Ninja, then backslash-escape for the shell.
      dest->push_back('\\');
      dest->push_back('$');
      dest->push_back(elem);
    } else if (elem == ':') {
      // Colon is the only other Ninja special char, which is not special to
      // the shell.
      dest->push_back('$');
      dest->push_back(':');
    } else if (static_cast<unsigned>(elem) >= 0x80 ||
               !kShellValid[static_cast<int>(elem)]) {
      // All other invalid shell chars get backslash-escaped.
      dest->push_back('\\');
      dest->push_back(elem);
    } else {
      // Everything else is a literal.
      dest->push_back(elem);
    }
  }
}

void EscapeStringToString(const base::StringPiece& str,
                          const EscapeOptions& options,
                          std::string* dest,
                          bool* needed_quoting) {
  switch (options.mode) {
    case ESCAPE_NONE:
      dest->append(str.data(), str.size());
      break;
    case ESCAPE_NINJA:
      EscapeStringToString_Ninja(str, options, dest, needed_quoting);
      break;
    case ESCAPE_NINJA_COMMAND:
      switch (options.platform) {
        case ESCAPE_PLATFORM_CURRENT:
#if defined(OS_WIN)
          EscapeStringToString_WindowsNinjaFork(str, options, dest,
                                                needed_quoting);
#else
          EscapeStringToString_PosixNinjaFork(str, options, dest,
                                              needed_quoting);
#endif
          break;
        case ESCAPE_PLATFORM_WIN:
          EscapeStringToString_WindowsNinjaFork(str, options, dest,
                                                needed_quoting);
          break;
        case ESCAPE_PLATFORM_POSIX:
          EscapeStringToString_PosixNinjaFork(str, options, dest,
                                              needed_quoting);
          break;
        default:
          NOTREACHED();
      }
      break;
    case ESCAPE_NINJA_PREFORMATTED_COMMAND:
      EscapeStringToString_NinjaPreformatted(str, dest);
      break;
    default:
      NOTREACHED();
  }
}

}  // namespace

std::string EscapeString(const base::StringPiece& str,
                         const EscapeOptions& options,
                         bool* needed_quoting) {
  std::string result;
  result.reserve(str.size() + 4);  // Guess we'll add a couple of extra chars.
  EscapeStringToString(str, options, &result, needed_quoting);
  return result;
}

void EscapeStringToStream(std::ostream& out,
                          const base::StringPiece& str,
                          const EscapeOptions& options) {
  std::string escaped;
  EscapeStringToString(str, options, &escaped, nullptr);
  if (!escaped.empty())
    out.write(escaped.data(), escaped.size());
}
