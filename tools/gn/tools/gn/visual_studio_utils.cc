// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/visual_studio_utils.h"

#include <vector>

#include "base/md5.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

CompilerOptions::CompilerOptions() = default;

CompilerOptions::~CompilerOptions() = default;

LinkerOptions::LinkerOptions() = default;

LinkerOptions::~LinkerOptions() = default;

std::string MakeGuid(const std::string& entry_path, const std::string& seed) {
  std::string str = base::ToUpperASCII(base::MD5String(seed + entry_path));
  return '{' + str.substr(0, 8) + '-' + str.substr(8, 4) + '-' +
         str.substr(12, 4) + '-' + str.substr(16, 4) + '-' +
         str.substr(20, 12) + '}';
}

#define SetOption(condition, member, value) \
  if (condition) {                          \
    options->member = value;                \
    return;                                 \
  }

#define AppendOption(condition, member, value, separator) \
  if (condition) {                                        \
    options->member += value + separator;                 \
    return;                                               \
  }

void ParseCompilerOption(const std::string& cflag, CompilerOptions* options) {
  if (cflag.size() > 2 && cflag[0] == '/') {
    switch (cflag[1]) {
      case 'F':
        AppendOption(cflag.size() > 3 && cflag[2] == 'I', forced_include_files,
                     cflag.substr(3), ';') break;

      case 'G':
        if (cflag[2] == 'S') {
          SetOption(cflag.size() == 3, buffer_security_check, "true")
              SetOption(cflag.size() == 4 && cflag[3] == '-',
                        buffer_security_check, "false")
        }
        break;

      case 'M':
        switch (cflag[2]) {
          case 'D':
            SetOption(cflag.size() == 3, runtime_library, "MultiThreadedDLL")
                SetOption(cflag.size() == 4 && cflag[3] == 'd', runtime_library,
                          "MultiThreadedDebugDLL") break;

          case 'T':
            SetOption(cflag.size() == 3, runtime_library, "MultiThreaded")
                SetOption(cflag.size() == 4 && cflag[3] == 'd', runtime_library,
                          "MultiThreadedDebug") break;
        }
        break;

      case 'O':
        switch (cflag[2]) {
          case '1':
            SetOption(cflag.size() == 3, optimization, "MinSpace") break;

          case '2':
            SetOption(cflag.size() == 3, optimization, "MaxSpeed") break;

          case 'd':
            SetOption(cflag.size() == 3, optimization, "Disabled") break;

          case 'x':
            SetOption(cflag.size() == 3, optimization, "Full") break;
        }
        break;

      case 'T':
        // Skip flags that cause treating all source files as C and C++ files.
        if (cflag.size() == 3 && (cflag[2] == 'C' || cflag[2] == 'P'))
          return;
        break;

      case 'W':
        switch (cflag[2]) {
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
            SetOption(cflag.size() == 3, warning_level,
                      std::string("Level") + cflag[2]) break;

          case 'X':
            SetOption(cflag.size() == 3, treat_warning_as_error, "true") break;
        }
        break;

      case 'w':
        AppendOption(cflag.size() > 3 && cflag[2] == 'd',
                     disable_specific_warnings, cflag.substr(3), ';') break;
    }
  }

  // Put everything else into additional_options.
  options->additional_options += cflag + ' ';
}

// Parses |ldflags| value and stores it in |options|.
void ParseLinkerOption(const std::string& ldflag, LinkerOptions* options) {
  const char kSubsytemPrefix[] = "/SUBSYSTEM:";
  if (base::StartsWith(ldflag, kSubsytemPrefix, base::CompareCase::SENSITIVE)) {
    const std::string subsystem(
        ldflag.begin() + std::string(kSubsytemPrefix).length(), ldflag.end());
    const std::vector<std::string> tokens = base::SplitString(
        subsystem, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (!tokens.empty())
      options->subsystem = tokens[0];
  }
}
