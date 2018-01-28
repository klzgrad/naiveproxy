// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "tools/gn/commands.h"
#include "tools/gn/header_checker.h"
#include "tools/gn/setup.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/switches.h"
#include "tools/gn/target.h"
#include "tools/gn/trace.h"

namespace commands {

const char kNoGnCheck_Help[] =
    R"(nogncheck: Skip an include line from checking.

  GN's header checker helps validate that the includes match the build
  dependency graph. Sometimes an include might be conditional or otherwise
  problematic, but you want to specifically allow it. In this case, it can be
  whitelisted.

  Include lines containing the substring "nogncheck" will be excluded from
  header checking. The most common case is a conditional include:

    #if defined(ENABLE_DOOM_MELON)
    #include "tools/doom_melon/doom_melon.h"  // nogncheck
    #endif

  If the build file has a conditional dependency on the corresponding target
  that matches the conditional include, everything will always link correctly:

    source_set("mytarget") {
      ...
      if (enable_doom_melon) {
        defines = [ "ENABLE_DOOM_MELON" ]
        deps += [ "//tools/doom_melon" ]
      }

  But GN's header checker does not understand preprocessor directives, won't
  know it matches the build dependencies, and will flag this include as
  incorrect when the condition is false.

More information

  The topic "gn help check" has general information on how checking works and
  advice on fixing problems. Targets can also opt-out of checking, see
  "gn help check_includes".
)";

const char kCheck[] = "check";
const char kCheck_HelpShort[] =
    "check: Check header dependencies.";
const char kCheck_Help[] =
    R"(gn check <out_dir> [<label_pattern>] [--force]

  GN's include header checker validates that the includes for C-like source
  files match the build dependency graph.

  "gn check" is the same thing as "gn gen" with the "--check" flag except that
  this command does not write out any build files. It's intended to be an easy
  way to manually trigger include file checking.

  The <label_pattern> can take exact labels or patterns that match more than
  one (although not general regular expressions). If specified, only those
  matching targets will be checked. See "gn help label_pattern" for details.

Command-specific switches

  --force
      Ignores specifications of "check_includes = false" and checks all
      target's files that match the target label.

What gets checked

  The .gn file may specify a list of targets to be checked. Only these targets
  will be checked if no label_pattern is specified on the command line.
  Otherwise, the command-line list is used instead. See "gn help dotfile".

  Targets can opt-out from checking with "check_includes = false" (see
  "gn help check_includes").

  For targets being checked:

    - GN opens all C-like source files in the targets to be checked and scans
      the top for includes.

    - Includes with a "nogncheck" annotation are skipped (see
      "gn help nogncheck").

    - Only includes using "quotes" are checked. <brackets> are assumed to be
      system includes.

    - Include paths are assumed to be relative to either the source root or the
      "root_gen_dir" and must include all the path components. (It might be
      nice in the future to incorporate GN's knowledge of the include path to
      handle other include styles.)

    - GN does not run the preprocessor so will not understand conditional
      includes.

    - Only includes matching known files in the build are checked: includes
      matching unknown paths are ignored.

  For an include to be valid:

    - The included file must be in the current target, or there must be a path
      following only public dependencies to a target with the file in it
      ("gn path" is a good way to diagnose problems).

    - There can be multiple targets with an included file: only one needs to be
      valid for the include to be allowed.

    - If there are only "sources" in a target, all are considered to be public
      and can be included by other targets with a valid public dependency path.

    - If a target lists files as "public", only those files are able to be
      included by other targets. Anything in the sources will be considered
      private and will not be includable regardless of dependency paths.

    - Ouptuts from actions are treated like public sources on that target.

    - A target can include headers from a target that depends on it if the
      other target is annotated accordingly. See "gn help
      allow_circular_includes_from".

Advice on fixing problems

  If you have a third party project that uses relative includes, it's generally
  best to exclude that target from checking altogether via
  "check_includes = false".

  If you have conditional includes, make sure the build conditions and the
  preprocessor conditions match, and annotate the line with "nogncheck" (see
  "gn help nogncheck" for an example).

  If two targets are hopelessly intertwined, use the
  "allow_circular_includes_from" annotation. Ideally each should have identical
  dependencies so configs inherited from those dependencies are consistent (see
  "gn help allow_circular_includes_from").

  If you have a standalone header file or files that need to be shared between
  a few targets, you can consider making a source_set listing only those
  headers as public sources. With only header files, the source set will be a
  no-op from a build perspective, but will give a central place to refer to
  those headers. That source set's files will still need to pass "gn check" in
  isolation.

  In rare cases it makes sense to list a header in more than one target if it
  could be considered conceptually a member of both.

Examples

  gn check out/Debug
      Check everything.

  gn check out/Default //foo:bar
      Check only the files in the //foo:bar target.

  gn check out/Default "//foo/*
      Check only the files in targets in the //foo directory tree.
)";

int RunCheck(const std::vector<std::string>& args) {
  if (args.size() != 1 && args.size() != 2) {
    Err(Location(), "You're holding it wrong.",
        "Usage: \"gn check <out_dir> [<target_label>]\"").PrintToStdout();
    return 1;
  }

  // Deliberately leaked to avoid expensive process teardown.
  Setup* setup = new Setup();
  if (!setup->DoSetup(args[0], false))
    return 1;
  if (!setup->Run())
    return 1;

  std::vector<const Target*> all_targets =
      setup->builder().GetAllResolvedTargets();

  bool filtered_by_build_config = false;
  std::vector<const Target*> targets_to_check;
  if (args.size() > 1) {
    // Compute the targets to check.
    std::vector<std::string> inputs(args.begin() + 1, args.end());
    UniqueVector<const Target*> target_matches;
    UniqueVector<const Config*> config_matches;
    UniqueVector<const Toolchain*> toolchain_matches;
    UniqueVector<SourceFile> file_matches;
    if (!ResolveFromCommandLineInput(setup, inputs, false,
                                     &target_matches, &config_matches,
                                     &toolchain_matches, &file_matches))
      return 1;

    if (target_matches.size() == 0) {
      OutputString("No matching targets.\n");
      return 1;
    }
    targets_to_check.insert(targets_to_check.begin(),
                            target_matches.begin(), target_matches.end());
  } else {
    // No argument means to check everything allowed by the filter in
    // the build config file.
    if (setup->check_patterns()) {
      FilterTargetsByPatterns(all_targets, *setup->check_patterns(),
                              &targets_to_check);
      filtered_by_build_config = targets_to_check.size() != all_targets.size();
    } else {
      // No global filter, check everything.
      targets_to_check = all_targets;
    }
  }

  const base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();
  bool force = cmdline->HasSwitch("force");

  if (!CheckPublicHeaders(&setup->build_settings(), all_targets,
                          targets_to_check, force))
    return 1;

  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kQuiet)) {
    if (filtered_by_build_config) {
      // Tell the user about the implicit filtering since this is obscure.
      OutputString(base::StringPrintf(
          "%d targets out of %d checked based on the check_targets defined in"
          " \".gn\".\n",
          static_cast<int>(targets_to_check.size()),
          static_cast<int>(all_targets.size())));
    }
    OutputString("Header dependency check OK\n", DECORATION_GREEN);
  }
  return 0;
}

bool CheckPublicHeaders(const BuildSettings* build_settings,
                        const std::vector<const Target*>& all_targets,
                        const std::vector<const Target*>& to_check,
                        bool force_check) {
  ScopedTrace trace(TraceItem::TRACE_CHECK_HEADERS, "Check headers");

  scoped_refptr<HeaderChecker> header_checker(
      new HeaderChecker(build_settings, all_targets));

  std::vector<Err> header_errors;
  header_checker->Run(to_check, force_check, &header_errors);
  for (size_t i = 0; i < header_errors.size(); i++) {
    if (i > 0)
      OutputString("___________________\n", DECORATION_YELLOW);
    header_errors[i].PrintToStdout();
  }
  return header_errors.empty();
}

}  // namespace commands
