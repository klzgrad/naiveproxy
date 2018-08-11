// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/sys_info.h"
#include "base/task_scheduler/task_scheduler.h"
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

int GetThreadCount() {
  std::string thread_count =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kThreads);

  // See if an override was specified on the command line.
  int result;
  if (!thread_count.empty() && base::StringToInt(thread_count, &result) &&
      result >= 1) {
    return result;
  }

  // Base the default number of worker threads on number of cores in the
  // system. When building large projects, the speed can be limited by how fast
  // the main thread can dispatch work and connect the dependency graph. If
  // there are too many worker threads, the main thread can be starved and it
  // will run slower overall.
  //
  // One less worker thread than the number of physical CPUs seems to be a
  // good value, both theoretically and experimentally. But always use at
  // least some workers to prevent us from being too sensitive to I/O latency
  // on low-end systems.
  //
  // The minimum thread count is based on measuring the optimal threads for the
  // Chrome build on a several-year-old 4-core MacBook.
  // Almost all CPUs now are hyperthreaded.
  int num_cores = base::SysInfo::NumberOfProcessors() / 2;
  return std::max(num_cores - 1, 8);
}

void StartTaskScheduler() {
  constexpr base::TimeDelta kSuggestedReclaimTime =
      base::TimeDelta::FromSeconds(30);

  constexpr int kBackgroundMaxThreads = 1;
  constexpr int kBackgroundBlockingMaxThreads = 2;
  const int kForegroundMaxThreads =
      std::max(1, base::SysInfo::NumberOfProcessors());
  const int foreground_blocking_max_threads = GetThreadCount();

  base::TaskScheduler::Create("gn");
  base::TaskScheduler::GetInstance()->Start(
      {{kBackgroundMaxThreads, kSuggestedReclaimTime},
       {kBackgroundBlockingMaxThreads, kSuggestedReclaimTime},
       {kForegroundMaxThreads, kSuggestedReclaimTime},
       {foreground_blocking_max_threads, kSuggestedReclaimTime}});
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
    base::MessageLoop message_loop;
    StartTaskScheduler();
    retval = found_command->second.runner(args);
    base::TaskScheduler::GetInstance()->Shutdown();
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
