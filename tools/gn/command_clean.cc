// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "tools/gn/commands.h"
#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/setup.h"

namespace {

// Extracts from a build.ninja the commands to run GN.
//
// The commands to run GN are the gn rule and build.ninja build step at the top
// of the build.ninja file. We want to keep these when deleting GN builds since
// we want to preserve the command-line flags to GN.
//
// On error, returns the empty string.
std::string ExtractGNBuildCommands(const base::FilePath& build_ninja_file) {
  std::string file_contents;
  if (!base::ReadFileToString(build_ninja_file, &file_contents))
    return std::string();

  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      file_contents, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

  std::string result;
  int num_blank_lines = 0;
  for (const auto& line : lines) {
    line.AppendToString(&result);
    result.push_back('\n');
    if (line.empty())
      ++num_blank_lines;
    if (num_blank_lines == 3)
      break;
  }

  return result;
}

}  // namespace

namespace commands {

const char kClean[] = "clean";
const char kClean_HelpShort[] =
    "clean: Cleans the output directory.";
const char kClean_Help[] =
    "gn clean <out_dir>\n"
    "\n"
    "  Deletes the contents of the output directory except for args.gn and\n"
    "  creates a Ninja build environment sufficient to regenerate the build.\n";

int RunClean(const std::vector<std::string>& args) {
  if (args.size() != 1) {
    Err(Location(), "You're holding it wrong.",
        "Usage: \"gn clean <out_dir>\"").PrintToStdout();
    return 1;
  }

  Setup* setup = new Setup;
  if (!setup->DoSetup(args[0], false))
    return 1;

  base::FilePath build_dir(setup->build_settings().GetFullPath(
      SourceDir(setup->build_settings().build_dir().value())));

  // NOTE: Not all GN builds have args.gn file hence we check here
  // if a build.ninja.d files exists instead.
  base::FilePath build_ninja_d_file = build_dir.AppendASCII("build.ninja.d");
  if (!base::PathExists(build_ninja_d_file)) {
    Err(Location(),
        base::StringPrintf(
            "%s does not look like a build directory.\n",
            FilePathToUTF8(build_ninja_d_file.DirName().value()).c_str()))
        .PrintToStdout();
    return 1;
  }

  // Erase everything but the args file, and write a dummy build.ninja file that
  // will automatically rerun GN the next time Ninja is run.
  base::FilePath build_ninja_file = build_dir.AppendASCII("build.ninja");
  std::string build_commands = ExtractGNBuildCommands(build_ninja_file);
  if (build_commands.empty()) {
    // Couldn't parse the build.ninja file.
    Err(Location(), "Couldn't read build.ninja in this directory.",
        "Try running \"gn gen\" on it and then re-running \"gn clean\".")
        .PrintToStdout();
    return 1;
  }

  // Read the args.gn file, if any. Not all GN builds have one.
  base::FilePath gn_args_file = build_dir.AppendASCII("args.gn");
  std::string args_contents;
  base::ReadFileToString(gn_args_file, &args_contents);

  base::DeleteFile(build_dir, true);

  // Put back the args.gn file (if any).
  base::CreateDirectory(build_dir);
  if (!args_contents.empty()) {
    if (base::WriteFile(gn_args_file, args_contents.data(),
                        static_cast<int>(args_contents.size())) == -1) {
      Err(Location(), std::string("Failed to write args.gn.")).PrintToStdout();
      return 1;
    }
  }

  // Write the build.ninja file sufficiently to regenerate itself.
  if (base::WriteFile(build_ninja_file, build_commands.data(),
                      static_cast<int>(build_commands.size())) == -1) {
    Err(Location(), std::string("Failed to write build.ninja."))
        .PrintToStdout();
    return 1;
  }

  // Write a .d file for the build which references a nonexistant file.
  // This will make Ninja always mark the build as dirty.
  std::string dummy_content("build.ninja: nonexistant_file.gn\n");
  if (base::WriteFile(build_ninja_d_file, dummy_content.data(),
                      static_cast<int>(dummy_content.size())) == -1) {
    Err(Location(), std::string("Failed to write build.ninja.d."))
        .PrintToStdout();
    return 1;
  }

  return 0;
}

}  // namespace commands
