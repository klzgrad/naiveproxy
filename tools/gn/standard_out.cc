// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/standard_out.h"

#include <stddef.h>

#include <vector>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "tools/gn/switches.h"

#if defined(OS_WIN)
#include <windows.h>
#else
#include <stdio.h>
#include <unistd.h>
#endif

namespace {

bool initialized = false;

#if defined(OS_WIN)
HANDLE hstdout;
WORD default_attributes;
#endif
bool is_console = false;

bool is_markdown = false;

void EnsureInitialized() {
  if (initialized)
    return;
  initialized = true;

  const base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();
  if (cmdline->HasSwitch(switches::kMarkdown)) {
    // Output help in Markdown's syntax, not color-highlighted.
    is_markdown = true;
  }

  if (cmdline->HasSwitch(switches::kNoColor)) {
    // Force color off.
    is_console = false;
    return;
  }

#if defined(OS_WIN)
  // On Windows, we can't force the color on. If the output handle isn't a
  // console, there's nothing we can do about it.
  hstdout = ::GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO info;
  is_console = !!::GetConsoleScreenBufferInfo(hstdout, &info);
  default_attributes = info.wAttributes;
#else
  if (cmdline->HasSwitch(switches::kColor))
    is_console = true;
  else
    is_console = isatty(fileno(stdout));
#endif
}

#if !defined(OS_WIN)
void WriteToStdOut(const std::string& output) {
  size_t written_bytes = fwrite(output.data(), 1, output.size(), stdout);
  DCHECK_EQ(output.size(), written_bytes);
}
#endif  // !defined(OS_WIN)

void OutputMarkdownDec(TextDecoration dec) {
  // The markdown rendering turns "dim" text to italics and any
  // other colored text to bold.

#if defined(OS_WIN)
  DWORD written = 0;
  if (dec == DECORATION_DIM)
    ::WriteFile(hstdout, "*", 1, &written, nullptr);
  else if (dec != DECORATION_NONE)
    ::WriteFile(hstdout, "**", 2, &written, nullptr);
#else
  if (dec == DECORATION_DIM)
    WriteToStdOut("*");
  else if (dec != DECORATION_NONE)
    WriteToStdOut("**");
#endif
}

}  // namespace

#if defined(OS_WIN)

void OutputString(const std::string& output, TextDecoration dec) {
  EnsureInitialized();
  DWORD written = 0;

  if (is_markdown) {
    OutputMarkdownDec(dec);
  } else if (is_console) {
    switch (dec) {
      case DECORATION_NONE:
        break;
      case DECORATION_DIM:
        ::SetConsoleTextAttribute(hstdout, FOREGROUND_INTENSITY);
        break;
      case DECORATION_RED:
        ::SetConsoleTextAttribute(hstdout,
                                  FOREGROUND_RED | FOREGROUND_INTENSITY);
        break;
      case DECORATION_GREEN:
        // Keep green non-bold.
        ::SetConsoleTextAttribute(hstdout, FOREGROUND_GREEN);
        break;
      case DECORATION_BLUE:
        ::SetConsoleTextAttribute(hstdout,
                                  FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        break;
      case DECORATION_YELLOW:
        ::SetConsoleTextAttribute(hstdout,
                                  FOREGROUND_RED | FOREGROUND_GREEN);
        break;
    }
  }

  std::string tmpstr = output;
  if (is_markdown && dec == DECORATION_YELLOW) {
    // https://code.google.com/p/gitiles/issues/detail?id=77
    // Gitiles will replace "--" with an em dash in non-code text.
    // Figuring out all instances of this might be difficult, but we can
    // at least escape the instances where this shows up in a heading.
    base::ReplaceSubstringsAfterOffset(&tmpstr, 0, "--", "\\--");
  }
  ::WriteFile(hstdout, tmpstr.c_str(), static_cast<DWORD>(tmpstr.size()),
              &written, nullptr);

  if (is_markdown) {
    OutputMarkdownDec(dec);
  } else if (is_console) {
    ::SetConsoleTextAttribute(hstdout, default_attributes);
  }
}

#else

void OutputString(const std::string& output, TextDecoration dec) {
  EnsureInitialized();
  if (is_markdown) {
    OutputMarkdownDec(dec);
  } else if (is_console) {
    switch (dec) {
      case DECORATION_NONE:
        break;
      case DECORATION_DIM:
        WriteToStdOut("\e[2m");
        break;
      case DECORATION_RED:
        WriteToStdOut("\e[31m\e[1m");
        break;
      case DECORATION_GREEN:
        WriteToStdOut("\e[32m");
        break;
      case DECORATION_BLUE:
        WriteToStdOut("\e[34m\e[1m");
        break;
      case DECORATION_YELLOW:
        WriteToStdOut("\e[33m\e[1m");
        break;
    }
  }

  std::string tmpstr = output;
  if (is_markdown && dec == DECORATION_YELLOW) {
    // https://code.google.com/p/gitiles/issues/detail?id=77
    // Gitiles will replace "--" with an em dash in non-code text.
    // Figuring out all instances of this might be difficult, but we can
    // at least escape the instances where this shows up in a heading.
    base::ReplaceSubstringsAfterOffset(&tmpstr, 0, "--", "\\--");
  }
  WriteToStdOut(tmpstr.data());

  if (is_markdown) {
    OutputMarkdownDec(dec);
  } else if (is_console && dec != DECORATION_NONE) {
    WriteToStdOut("\e[0m");
  }
}

#endif

void PrintSectionHelp(const std::string& line,
                      const std::string& topic,
                      const std::string& tag) {
  EnsureInitialized();

  if (is_markdown) {
    OutputString("*   [" + line + "](#" + tag + ")\n");
  } else if (topic.size()) {
    OutputString("\n" + line + " (type \"gn help " + topic +
                 "\" for more help):\n");
  } else {
    OutputString("\n" + line + ":\n");
  }
}

void PrintShortHelp(const std::string& line) {
  EnsureInitialized();

  size_t colon_offset = line.find(':');
  size_t first_normal = 0;
  if (colon_offset != std::string::npos) {
    if (is_markdown) {
      OutputString("    *   [" + line + "](#" + line.substr(0, colon_offset) +
                   ")\n");
    } else {
      OutputString("  " + line.substr(0, colon_offset), DECORATION_YELLOW);
      first_normal = colon_offset;
    }
  } else if (is_markdown) {
    OutputString("    *   [" + line + "](" + line + ")\n");
  }

  if (is_markdown)
    return;

  // See if the colon is followed by a " [" and if so, dim the contents of [ ].
  if (first_normal > 0 &&
      line.size() > first_normal + 2 &&
      line[first_normal + 1] == ' ' && line[first_normal + 2] == '[') {
    size_t begin_bracket = first_normal + 2;
    OutputString(": ");
    first_normal = line.find(']', begin_bracket);
    if (first_normal == std::string::npos)
      first_normal = line.size();
    else
      first_normal++;
    OutputString(line.substr(begin_bracket, first_normal - begin_bracket),
                 DECORATION_DIM);
  }

  OutputString(line.substr(first_normal) + "\n");
}

void PrintLongHelp(const std::string& text, const std::string& tag) {
  EnsureInitialized();

  bool first_header = true;
  bool in_body = false;
  std::size_t empty_lines = 0;
  for (const std::string& line : base::SplitString(
           text, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    // Check for a heading line.
    if (!line.empty() && line[0] != ' ') {
      // New paragraph, just skip any trailing empty lines.
      empty_lines = 0;

      if (is_markdown) {
        // GN's block-level formatting is converted to markdown as follows:
        // * The first heading is treated as an H3.
        // * Subsequent heading are treated as H4s.
        // * Any other text is wrapped in a code block and displayed as-is.
        //
        // Span-level formatting (the decorations) is converted inside
        // OutputString().
        if (in_body) {
          OutputString("```\n\n", DECORATION_NONE);
          in_body = false;
        }

        if (first_header) {
          std::string the_tag = tag;
          if (the_tag.size() == 0) {
            if (line.substr(0, 2) == "gn") {
              the_tag = line.substr(3, line.substr(3).find(' '));
            } else {
              the_tag = line.substr(0, line.find(':'));
            }
          }
          OutputString("### <a name=\"" + the_tag + "\"></a>", DECORATION_NONE);
          first_header = false;
        } else {
          OutputString("#### ", DECORATION_NONE);
        }
      }

      // Highlight up to the colon (if any).
      size_t chars_to_highlight = line.find(':');
      if (chars_to_highlight == std::string::npos)
        chars_to_highlight = line.size();

      OutputString(line.substr(0, chars_to_highlight), DECORATION_YELLOW);
      OutputString(line.substr(chars_to_highlight));
      OutputString("\n");
      continue;
    } else if (is_markdown && !line.empty() && !in_body) {
      OutputString("```\n", DECORATION_NONE);
      in_body = true;
    }

    // We buffer empty lines, so we can skip them if needed
    // (i.e. new paragraph body, end of final paragraph body).
    if (in_body && is_markdown) {
      if (!line.empty() && empty_lines != 0) {
        OutputString(std::string(empty_lines, '\n'));
        empty_lines = 0;
      } else if (line.empty()) {
        ++empty_lines;
        continue;
      }
    }

    // Check for a comment.
    TextDecoration dec = DECORATION_NONE;
    for (const auto& elem : line) {
      if (elem == '#' && !is_markdown) {
        // Got a comment, draw dimmed.
        dec = DECORATION_DIM;
        break;
      } else if (elem != ' ') {
        break;
      }
    }

    OutputString(line + "\n", dec);
  }

  if (is_markdown && in_body)
    OutputString("```\n");
}

