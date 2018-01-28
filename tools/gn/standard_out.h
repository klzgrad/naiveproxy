// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_STANDARD_OUT_H_
#define TOOLS_GN_STANDARD_OUT_H_

#include <string>

enum TextDecoration {
  DECORATION_NONE = 0,
  DECORATION_DIM,
  DECORATION_RED,
  DECORATION_GREEN,
  DECORATION_BLUE,
  DECORATION_YELLOW
};

void OutputString(const std::string& output,
                  TextDecoration dec = DECORATION_NONE);

// If printing markdown, this generates table-of-contents entries with
// links to the actual help; otherwise, prints a one-line description.
void PrintSectionHelp(const std::string& line,
                      const std::string& topic,
                      const std::string& tag);

// Prints a line for a command, assuming there is a colon. Everything before
// the colon is the command (and is highlighted). After the colon if there is
// a square bracket, the contents of the bracket is dimmed.
//
// The line is indented 2 spaces.
void PrintShortHelp(const std::string& line);

// Rules:
// - Lines beginning with non-whitespace are highlighted up to the first
//   colon (or the whole line if not).
// - Lines whose first non-whitespace character is a # are dimmed.
void PrintLongHelp(const std::string& text, const std::string& tag = "");

#endif  // TOOLS_GN_STANDARD_OUT_H_
