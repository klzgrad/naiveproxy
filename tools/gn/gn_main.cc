// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "tools/gn/commands.h"
#include "tools/gn/err.h"
#include "tools/gn/location.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/switches.h"

// Only the GN-generated build makes this header for now.
// TODO(brettw) consider adding this if we need it in GYP.
#if defined(GN_BUILD)
#include "tools/gn/last_commit_position.h"
#else
#define LAST_COMMIT_POSITION "UNKNOWN"
#endif

namespace {

std::vector<std::string> GetArgs(const base::CommandLine& cmdline) {
  base::CommandLine::StringVector in_args = cmdline.GetArgs();
#if defined(OS_WIN)
  std::vector<std::string> out_args;
  for (const auto& arg : in_args)
    out_args.push_back(base::WideToUTF8(arg));
  return out_args;
#else
  return in_args;
#endif
}

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
#if defined(OS_WIN)
  base::CommandLine::set_slash_is_not_a_switch();
#endif
  base::CommandLine::Init(argc, argv);

  const base::CommandLine& cmdline = *base::CommandLine::ForCurrentProcess();
  std::vector<std::string> args = GetArgs(cmdline);

  std::string command;
  if (cmdline.HasSwitch("help") || cmdline.HasSwitch("h")) {
    // Make "-h" and "--help" default to help command.
    command = commands::kHelp;
  } else if (cmdline.HasSwitch(switches::kVersion)) {
    // Make "--version" print the version and exit.
    OutputString(std::string(LAST_COMMIT_POSITION) + "\n");
    exit(0);
  } else if (args.empty()) {
    // No command, print error and exit.
    Err(Location(), "No command specified.",
        "Most commonly you want \"gn gen <out_dir>\" to make a build dir.\n"
        "Or try \"gn help\" for more commands.").PrintToStdout();
    return 1;
  } else {
    command = args[0];
    args.erase(args.begin());
  }

  const commands::CommandInfoMap& command_map = commands::GetCommands();
  commands::CommandInfoMap::const_iterator found_command =
      command_map.find(command);

  int retval;
  if (found_command != command_map.end()) {
    retval = found_command->second.runner(args);
  } else {
    Err(Location(), "Command \"" + command + "\" unknown.").PrintToStdout();
    OutputString(
        "Available commands (type \"gn help <command>\" for more details):\n");
    for (const auto& cmd : commands::GetCommands())
      PrintShortHelp(cmd.second.help_short);

    retval = 1;
  }

  exit(retval);  // Don't free memory, it can be really slow!
}
