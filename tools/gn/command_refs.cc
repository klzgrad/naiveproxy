// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <map>
#include <set>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "tools/gn/commands.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/input_file.h"
#include "tools/gn/item.h"
#include "tools/gn/setup.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/switches.h"
#include "tools/gn/target.h"

namespace commands {

namespace {

typedef std::set<const Target*> TargetSet;
typedef std::vector<const Target*> TargetVector;

// Maps targets to the list of targets that depend on them.
typedef std::multimap<const Target*, const Target*> DepMap;

// Populates the reverse dependency map for the targets in the Setup.
void FillDepMap(Setup* setup, DepMap* dep_map) {
  for (auto* target : setup->builder().GetAllResolvedTargets()) {
    for (const auto& dep_pair : target->GetDeps(Target::DEPS_ALL))
      dep_map->insert(std::make_pair(dep_pair.ptr, target));
  }
}

// Forward declaration for function below.
size_t RecursivePrintTargetDeps(const DepMap& dep_map,
                               const Target* target,
                               TargetSet* seen_targets,
                               int indent_level);

// Prints the target and its dependencies in tree form. If the set is non-null,
// new targets encountered will be added to the set, and if a ref is in the set
// already, it will not be recused into. When the set is null, all refs will be
// printed.
//
// Returns the number of items printed.
size_t RecursivePrintTarget(const DepMap& dep_map,
                          const Target* target,
                          TargetSet* seen_targets,
                          int indent_level) {
  std::string indent(indent_level * 2, ' ');
  size_t count = 1;

  // Only print the toolchain for non-default-toolchain targets.
  OutputString(indent + target->label().GetUserVisibleName(
      !target->settings()->is_default()));

  bool print_children = true;
  if (seen_targets) {
    if (seen_targets->find(target) == seen_targets->end()) {
      // New target, mark it visited.
      seen_targets->insert(target);
    } else {
      // Already seen.
      print_children = false;
      // Only print "..." if something is actually elided, which means that
      // the current target has children.
      if (dep_map.lower_bound(target) != dep_map.upper_bound(target))
        OutputString("...");
    }
  }

  OutputString("\n");
  if (print_children) {
    count += RecursivePrintTargetDeps(dep_map, target, seen_targets,
                                      indent_level + 1);
  }
  return count;
}

// Prints refs of the given target (not the target itself). See
// RecursivePrintTarget.
size_t RecursivePrintTargetDeps(const DepMap& dep_map,
                              const Target* target,
                              TargetSet* seen_targets,
                              int indent_level) {
  DepMap::const_iterator dep_begin = dep_map.lower_bound(target);
  DepMap::const_iterator dep_end = dep_map.upper_bound(target);
  size_t count = 0;
  for (DepMap::const_iterator cur_dep = dep_begin;
       cur_dep != dep_end; cur_dep++) {
    count += RecursivePrintTarget(dep_map, cur_dep->second, seen_targets,
                                  indent_level);
  }
  return count;
}

void RecursiveCollectChildRefs(const DepMap& dep_map,
                               const Target* target,
                               TargetSet* results);

// Recursively finds all targets that reference the given one, and additionally
// adds the current one to the list.
void RecursiveCollectRefs(const DepMap& dep_map,
                          const Target* target,
                          TargetSet* results) {
  if (results->find(target) != results->end())
    return;  // Already found this target.
  results->insert(target);
  RecursiveCollectChildRefs(dep_map, target, results);
}

// Recursively finds all targets that reference the given one.
void RecursiveCollectChildRefs(const DepMap& dep_map,
                               const Target* target,
                               TargetSet* results) {
  DepMap::const_iterator dep_begin = dep_map.lower_bound(target);
  DepMap::const_iterator dep_end = dep_map.upper_bound(target);
  for (DepMap::const_iterator cur_dep = dep_begin;
       cur_dep != dep_end; cur_dep++)
    RecursiveCollectRefs(dep_map, cur_dep->second, results);
}

bool TargetContainsFile(const Target* target, const SourceFile& file) {
  for (const auto& cur_file : target->sources()) {
    if (cur_file == file)
      return true;
  }
  for (const auto& cur_file : target->public_headers()) {
    if (cur_file == file)
      return true;
  }
  for (const auto& cur_file : target->inputs()) {
    if (cur_file == file)
      return true;
  }
  for (const auto& cur_file : target->data()) {
    if (cur_file == file.value())
      return true;
    if (cur_file.back() == '/' &&
        base::StartsWith(file.value(), cur_file, base::CompareCase::SENSITIVE))
      return true;
  }

  if (target->action_values().script().value() == file.value())
    return true;

  std::vector<SourceFile> outputs;
  target->action_values().GetOutputsAsSourceFiles(target, &outputs);
  for (const auto& cur_file : outputs) {
    if (cur_file == file)
      return true;
  }
  return false;
}

void GetTargetsContainingFile(Setup* setup,
                              const std::vector<const Target*>& all_targets,
                              const SourceFile& file,
                              bool all_toolchains,
                              UniqueVector<const Target*>* matches) {
  Label default_toolchain = setup->loader()->default_toolchain_label();
  for (auto* target : all_targets) {
    if (!all_toolchains) {
      // Only check targets in the default toolchain.
      if (target->label().GetToolchainLabel() != default_toolchain)
        continue;
    }
    if (TargetContainsFile(target, file))
      matches->push_back(target);
  }
}

bool TargetReferencesConfig(const Target* target, const Config* config) {
  for (const LabelConfigPair& cur : target->configs()) {
    if (cur.ptr == config)
      return true;
  }
  for (const LabelConfigPair& cur : target->public_configs()) {
    if (cur.ptr == config)
      return true;
  }
  return false;
}

void GetTargetsReferencingConfig(Setup* setup,
                                 const std::vector<const Target*>& all_targets,
                                 const Config* config,
                                 bool all_toolchains,
                                 UniqueVector<const Target*>* matches) {
  Label default_toolchain = setup->loader()->default_toolchain_label();
  for (auto* target : all_targets) {
    if (!all_toolchains) {
      // Only check targets in the default toolchain.
      if (target->label().GetToolchainLabel() != default_toolchain)
        continue;
    }
    if (TargetReferencesConfig(target, config))
      matches->push_back(target);
  }
}

// Returns the number of matches printed.
size_t DoTreeOutput(const DepMap& dep_map,
                    const UniqueVector<const Target*>& implicit_target_matches,
                    const UniqueVector<const Target*>& explicit_target_matches,
                    bool all) {
  TargetSet seen_targets;
  size_t count = 0;

  // Implicit targets don't get printed themselves.
  for (const Target* target : implicit_target_matches) {
    if (all)
      count += RecursivePrintTargetDeps(dep_map, target, nullptr, 0);
    else
      count += RecursivePrintTargetDeps(dep_map, target, &seen_targets, 0);
  }

  // Explicit targets appear in the output.
  for (const Target* target : implicit_target_matches) {
    if (all)
      count += RecursivePrintTarget(dep_map, target, nullptr, 0);
    else
      count += RecursivePrintTarget(dep_map, target, &seen_targets, 0);
  }

  return count;
}

// Returns the number of matches printed.
size_t DoAllListOutput(
    const DepMap& dep_map,
    const UniqueVector<const Target*>& implicit_target_matches,
    const UniqueVector<const Target*>& explicit_target_matches) {
  // Output recursive dependencies, uniquified and flattened.
  TargetSet results;

  for (const Target* target : implicit_target_matches)
    RecursiveCollectChildRefs(dep_map, target, &results);
  for (const Target* target : explicit_target_matches) {
    // Explicit targets also get added to the output themselves.
    results.insert(target);
    RecursiveCollectChildRefs(dep_map, target, &results);
  }

  FilterAndPrintTargetSet(false, results);
  return results.size();
}

// Returns the number of matches printed.
size_t DoDirectListOutput(
    const DepMap& dep_map,
    const UniqueVector<const Target*>& implicit_target_matches,
    const UniqueVector<const Target*>& explicit_target_matches) {
  TargetSet results;

  // Output everything that refers to the implicit ones.
  for (const Target* target : implicit_target_matches) {
    DepMap::const_iterator dep_begin = dep_map.lower_bound(target);
    DepMap::const_iterator dep_end = dep_map.upper_bound(target);
    for (DepMap::const_iterator cur_dep = dep_begin;
         cur_dep != dep_end; cur_dep++)
      results.insert(cur_dep->second);
  }

  // And just output the explicit ones directly (these are the target matches
  // when referring to what references a file or config).
  for (const Target* target : explicit_target_matches)
    results.insert(target);

  FilterAndPrintTargetSet(false, results);
  return results.size();
}

}  // namespace

const char kRefs[] = "refs";
const char kRefs_HelpShort[] =
    "refs: Find stuff referencing a target or file.";
const char kRefs_Help[] =
    R"(gn refs <out_dir> (<label_pattern>|<label>|<file>|@<response_file>)*
        [--all] [--all-toolchains] [--as=...] [--testonly=...] [--type=...]

  Finds reverse dependencies (which targets reference something). The input is
  a list containing:

   - Target label: The result will be which targets depend on it.

   - Config label: The result will be which targets list the given config in
     its "configs" or "public_configs" list.

   - Label pattern: The result will be which targets depend on any target
     matching the given pattern. Patterns will not match configs. These are not
     general regular expressions, see "gn help label_pattern" for details.

   - File name: The result will be which targets list the given file in its
     "inputs", "sources", "public", "data", or "outputs". Any input that does
     not contain wildcards and does not match a target or a config will be
     treated as a file.

   - Response file: If the input starts with an "@", it will be interpreted as
     a path to a file containing a list of labels or file names, one per line.
     This allows us to handle long lists of inputs without worrying about
     command line limits.

Options

  --all
      When used without --tree, will recurse and display all unique
      dependencies of the given targets. For example, if the input is a target,
      this will output all targets that depend directly or indirectly on the
      input. If the input is a file, this will output all targets that depend
      directly or indirectly on that file.

      When used with --tree, turns off eliding to show a complete tree.
)"

ALL_TOOLCHAINS_SWITCH_HELP
"\n"
TARGET_PRINTING_MODE_COMMAND_LINE_HELP

R"(
  -q
     Quiet. If nothing matches, don't print any output. Without this option, if
     there are no matches there will be an informational message printed which
     might interfere with scripts processing the output.
)"

TARGET_TESTONLY_FILTER_COMMAND_LINE_HELP

R"(
  --tree
      Outputs a reverse dependency tree from the given target. Duplicates will
      be elided. Combine with --all to see a full dependency tree.

      Tree output can not be used with the filtering or output flags: --as,
      --type, --testonly.
)"

TARGET_TYPE_FILTER_COMMAND_LINE_HELP

R"(

Examples (target input)

  gn refs out/Debug //tools/gn:gn
      Find all targets depending on the given exact target name.

  gn refs out/Debug //base:i18n --as=buildfiles | xargs gvim
      Edit all .gn files containing references to //base:i18n

  gn refs out/Debug //base --all
      List all targets depending directly or indirectly on //base:base.

  gn refs out/Debug "//base/*"
      List all targets depending directly on any target in //base or
      its subdirectories.

  gn refs out/Debug "//base:*"
      List all targets depending directly on any target in
      //base/BUILD.gn.

  gn refs out/Debug //base --tree
      Print a reverse dependency tree of //base:base

Examples (file input)

  gn refs out/Debug //base/macros.h
      Print target(s) listing //base/macros.h as a source.

  gn refs out/Debug //base/macros.h --tree
      Display a reverse dependency tree to get to the given file. This
      will show how dependencies will reference that file.

  gn refs out/Debug //base/macros.h //base/at_exit.h --all
      Display all unique targets with some dependency path to a target
      containing either of the given files as a source.

  gn refs out/Debug //base/macros.h --testonly=true --type=executable
          --all --as=output
      Display the executable file names of all test executables
      potentially affected by a change to the given file.
)";

int RunRefs(const std::vector<std::string>& args) {
  if (args.size() <= 1) {
    Err(Location(), "You're holding it wrong.",
        "Usage: \"gn refs <out_dir> (<label_pattern>|<file>)*\"")
        .PrintToStdout();
    return 1;
  }

  const base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();
  bool tree = cmdline->HasSwitch("tree");
  bool all = cmdline->HasSwitch("all");
  bool all_toolchains = cmdline->HasSwitch(switches::kAllToolchains);

  Setup* setup = new Setup;
  if (!setup->DoSetup(args[0], false) || !setup->Run())
    return 1;

  // The inputs are everything but the first arg (which is the build dir).
  std::vector<std::string> inputs;
  for (size_t i = 1; i < args.size(); i++) {
    if (args[i][0] == '@') {
      // The argument is as a path to a response file.
      std::string contents;
      bool ret = base::ReadFileToString(UTF8ToFilePath(args[i].substr(1)),
                                        &contents);
      if (!ret) {
        Err(Location(), "Response file " + args[i].substr(1) + " not found.")
            .PrintToStdout();
        return 1;
      }
      for (const std::string& line : base::SplitString(
               contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
        if (!line.empty())
          inputs.push_back(line);
      }
    } else {
      // The argument is a label or a path.
      inputs.push_back(args[i]);
    }
  }

  // Get the matches for the command-line input.
  UniqueVector<const Target*> target_matches;
  UniqueVector<const Config*> config_matches;
  UniqueVector<const Toolchain*> toolchain_matches;
  UniqueVector<SourceFile> file_matches;
  if (!ResolveFromCommandLineInput(setup, inputs, all_toolchains,
                                   &target_matches, &config_matches,
                                   &toolchain_matches, &file_matches))
    return 1;

  // When you give a file or config as an input, you want the targets that are
  // associated with it. We don't want to just append this to the
  // target_matches, however, since these targets should actually be listed in
  // the output, while for normal targets you don't want to see the inputs,
  // only what refers to them.
  std::vector<const Target*> all_targets =
      setup->builder().GetAllResolvedTargets();
  UniqueVector<const Target*> explicit_target_matches;
  for (const auto& file : file_matches) {
    GetTargetsContainingFile(setup, all_targets, file, all_toolchains,
                             &explicit_target_matches);
  }
  for (auto* config : config_matches) {
    GetTargetsReferencingConfig(setup, all_targets, config, all_toolchains,
                                &explicit_target_matches);
  }

  // Tell the user if their input matches no files or labels. We need to check
  // both that it matched no targets and no configs. File input will already
  // have been converted to targets at this point. Configs will have been
  // converted to targets also, but there could be no targets referencing the
  // config, which is different than no config with that name.
  bool quiet = cmdline->HasSwitch("q");
  if (!quiet && config_matches.empty() &&
      explicit_target_matches.empty() && target_matches.empty()) {
    OutputString("The input matches no targets, configs, or files.\n",
                 DECORATION_YELLOW);
    return 1;
  }

  // Construct the reverse dependency tree.
  DepMap dep_map;
  FillDepMap(setup, &dep_map);

  size_t cnt = 0;
  if (tree)
    cnt = DoTreeOutput(dep_map, target_matches, explicit_target_matches, all);
  else if (all)
    cnt = DoAllListOutput(dep_map, target_matches, explicit_target_matches);
  else
    cnt = DoDirectListOutput(dep_map, target_matches, explicit_target_matches);

  // If you ask for the references of a valid target, but that target has
  // nothing referencing it, we'll get here without having printed anything.
  if (!quiet && cnt == 0)
    OutputString("Nothing references this.\n", DECORATION_YELLOW);

  return 0;
}

}  // namespace commands
