// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_VISUAL_STUDIO_UTILS_H_
#define TOOLS_GN_VISUAL_STUDIO_UTILS_H_

#include <string>

// Some compiler options which will be written to project file. We don't need to
// specify all options because generated project file is going to be used only
// for compilation of single file. For real build ninja files are used.
struct CompilerOptions {
  CompilerOptions();
  ~CompilerOptions();

  std::string additional_options;
  std::string buffer_security_check;
  std::string forced_include_files;
  std::string disable_specific_warnings;
  std::string optimization;
  std::string runtime_library;
  std::string treat_warning_as_error;
  std::string warning_level;
};

// Some linker options which will be written to project file. We don't need to
// specify all options because generated project file is going to be used only
// for compilation of single file. For real build ninja files are used.
struct LinkerOptions {
  LinkerOptions();
  ~LinkerOptions();

  std::string subsystem;
};

// Generates something which looks like a GUID, but depends only on the name and
// seed. This means the same name / seed will always generate the same GUID, so
// that projects and solutions which refer to each other can explicitly
// determine the GUID to refer to explicitly. It also means that the GUID will
// not change when the project for a target is rebuilt.
std::string MakeGuid(const std::string& entry_path, const std::string& seed);

// Parses |cflag| value and stores it in |options|.
void ParseCompilerOption(const std::string& cflag, CompilerOptions* options);

// Parses |ldflags| value and stores it in |options|.
void ParseLinkerOption(const std::string& ldflag, LinkerOptions* options);

#endif  // TOOLS_GN_VISUAL_STUDIO_UTILS_H_
