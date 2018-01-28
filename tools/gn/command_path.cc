// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <algorithm>

#include "base/command_line.h"
#include "base/containers/hash_tables.h"
#include "base/strings/stringprintf.h"
#include "tools/gn/commands.h"
#include "tools/gn/setup.h"
#include "tools/gn/standard_out.h"

namespace commands {

namespace {

enum class DepType {
  NONE,
  PUBLIC,
  PRIVATE,
  DATA
};

// The dependency paths are stored in a vector. Assuming the chain:
//    A --[public]--> B --[private]--> C
// The stack will look like:
//    [0] = A, NONE (this has no dep type since nobody depends on it)
//    [1] = B, PUBLIC
//    [2] = C, PRIVATE
using TargetDep = std::pair<const Target*, DepType>;
using PathVector = std::vector<TargetDep>;

// How to search.
enum class PrivateDeps { INCLUDE, EXCLUDE };
enum class DataDeps { INCLUDE, EXCLUDE };
enum class PrintWhat { ONE, ALL };

struct Options {
  Options()
      : print_what(PrintWhat::ONE),
        public_only(false),
        with_data(false) {
  }

  PrintWhat print_what;
  bool public_only;
  bool with_data;
};

typedef std::list<PathVector> WorkQueue;

struct Stats {
  Stats() : public_paths(0), other_paths(0) {
  }

  int total_paths() const { return public_paths + other_paths; }

  int public_paths;
  int other_paths;

  // Stores targets that have a path to the destination, and whether that
  // path is public, private, or data.
  std::map<const Target*, DepType> found_paths;
};

// If the implicit_last_dep is not "none", this type indicates the
// classification of the elided last part of path.
DepType ClassifyPath(const PathVector& path, DepType implicit_last_dep) {
  DepType result;
  if (implicit_last_dep != DepType::NONE)
    result = implicit_last_dep;
  else
    result = DepType::PUBLIC;

  // Skip the 0th one since that is always NONE.
  for (size_t i = 1; i < path.size(); i++) {
    // PRIVATE overrides PUBLIC, and DATA overrides everything (the idea is
    // to find the worst link in the path).
    if (path[i].second == DepType::PRIVATE) {
      if (result == DepType::PUBLIC)
        result = DepType::PRIVATE;
    } else if (path[i].second == DepType::DATA) {
      result = DepType::DATA;
    }
  }
  return result;
}

const char* StringForDepType(DepType type) {
  switch(type) {
    case DepType::PUBLIC:
      return "public";
    case DepType::PRIVATE:
      return "private";
    case DepType::DATA:
      return "data";
      break;
    case DepType::NONE:
    default:
      return "";
  }
}

// Prints the given path. If the implicit_last_dep is not "none", the last
// dependency will show an elided dependency with the given annotation.
void PrintPath(const PathVector& path, DepType implicit_last_dep) {
  if (path.empty())
    return;

  // Don't print toolchains unless they differ from the first target.
  const Label& default_toolchain = path[0].first->label().GetToolchainLabel();

  for (size_t i = 0; i < path.size(); i++) {
    OutputString(path[i].first->label().GetUserVisibleName(default_toolchain));

    // Output dependency type.
    if (i == path.size() - 1) {
      // Last one either gets the implicit last dep type or nothing.
      if (implicit_last_dep != DepType::NONE) {
        OutputString(std::string(" --> see ") +
                     StringForDepType(implicit_last_dep) +
                     " chain printed above...", DECORATION_DIM);
      }
    } else {
      // Take type from the next entry.
      OutputString(std::string(" --[") + StringForDepType(path[i + 1].second) +
                   "]-->", DECORATION_DIM);
    }
    OutputString("\n");
  }

  OutputString("\n");
}

void InsertTargetsIntoFoundPaths(const PathVector& path,
                                 DepType implicit_last_dep,
                                 Stats* stats) {
  DepType type = ClassifyPath(path, implicit_last_dep);

  bool inserted = false;

  // Don't try to insert the 0th item in the list which is the "from" target.
  // The search will be run more than once (for the different path types) and
  // if the "from" target was in the list, subsequent passes could never run
  // the starting point is alredy in the list of targets considered).
  //
  // One might imagine an alternate implementation where all items are counted
  // here but the "from" item is erased at the beginning of each search, but
  // that will mess up the metrics (the private search pass will find the
  // same public paths as the previous public pass, "inserted" will be true
  // here since the item wasn't found, and the public path will be
  // double-counted in the stats.
  for (size_t i = 1; i < path.size(); i++) {
    const auto& pair = path[i];

    // Don't overwrite an existing one. The algorithm works by first doing
    // public, then private, then data, so anything already there is guaranteed
    // at least as good as our addition.
    if (stats->found_paths.find(pair.first) == stats->found_paths.end()) {
      stats->found_paths.insert(std::make_pair(pair.first, type));
      inserted = true;
    }
  }

  if (inserted) {
    // Only count this path in the stats if any part of it was actually new.
    if (type == DepType::PUBLIC)
      stats->public_paths++;
    else
      stats->other_paths++;
  }
}

void BreadthFirstSearch(const Target* from, const Target* to,
                        PrivateDeps private_deps, DataDeps data_deps,
                        PrintWhat print_what,
                        Stats* stats) {
  // Seed the initial stack with just the "from" target.
  PathVector initial_stack;
  initial_stack.emplace_back(from, DepType::NONE);
  WorkQueue work_queue;
  work_queue.push_back(initial_stack);

  // Track checked targets to avoid checking the same once more than once.
  std::set<const Target*> visited;

  while (!work_queue.empty()) {
    PathVector current_path = work_queue.front();
    work_queue.pop_front();

    const Target* current_target = current_path.back().first;

    if (current_target == to) {
      // Found a new path.
      if (stats->total_paths() == 0 || print_what == PrintWhat::ALL)
        PrintPath(current_path, DepType::NONE);

      // Insert all nodes on the path into the found paths list. Since we're
      // doing search breadth first, we know that the current path is the best
      // path for all nodes on it.
      InsertTargetsIntoFoundPaths(current_path, DepType::NONE, stats);
    } else {
      // Check for a path that connects to an already known-good one. Printing
      // this here will mean the results aren't strictly in depth-first order
      // since there could be many items on the found path this connects to.
      // Doing this here will mean that the output is sorted by length of items
      // printed (with the redundant parts of the path omitted) rather than
      // complete path length.
      const auto& found_current_target =
          stats->found_paths.find(current_target);
      if (found_current_target != stats->found_paths.end()) {
        if (stats->total_paths() == 0 || print_what == PrintWhat::ALL)
          PrintPath(current_path, found_current_target->second);

        // Insert all nodes on the path into the found paths list since we know
        // everything along this path also leads to the destination.
        InsertTargetsIntoFoundPaths(current_path, found_current_target->second,
                                    stats);
        continue;
      }
    }

    // If we've already checked this one, stop. This should be after the above
    // check for a known-good check, because known-good ones will always have
    // been previously visited.
    if (visited.find(current_target) == visited.end())
      visited.insert(current_target);
    else
      continue;

    // Add public deps for this target to the queue.
    for (const auto& pair : current_target->public_deps()) {
      work_queue.push_back(current_path);
      work_queue.back().push_back(TargetDep(pair.ptr, DepType::PUBLIC));
    }

    if (private_deps == PrivateDeps::INCLUDE) {
      // Add private deps.
      for (const auto& pair : current_target->private_deps()) {
        work_queue.push_back(current_path);
        work_queue.back().push_back(
            TargetDep(pair.ptr, DepType::PRIVATE));
      }
    }

    if (data_deps == DataDeps::INCLUDE) {
      // Add data deps.
      for (const auto& pair : current_target->data_deps()) {
        work_queue.push_back(current_path);
        work_queue.back().push_back(TargetDep(pair.ptr, DepType::DATA));
      }
    }
  }
}

void DoSearch(const Target* from, const Target* to, const Options& options,
              Stats* stats) {
  BreadthFirstSearch(from, to, PrivateDeps::EXCLUDE, DataDeps::EXCLUDE,
                     options.print_what, stats);
  if (!options.public_only) {
    // Check private deps.
    BreadthFirstSearch(from, to, PrivateDeps::INCLUDE,
                       DataDeps::EXCLUDE, options.print_what, stats);
    if (options.with_data) {
      // Check data deps.
      BreadthFirstSearch(from, to, PrivateDeps::INCLUDE,
                         DataDeps::INCLUDE, options.print_what, stats);
    }
  }
}

}  // namespace

const char kPath[] = "path";
const char kPath_HelpShort[] =
    "path: Find paths between two targets.";
const char kPath_Help[] =
    R"(gn path <out_dir> <target_one> <target_two>

  Finds paths of dependencies between two targets. Each unique path will be
  printed in one group, and groups will be separate by newlines. The two
  targets can appear in either order (paths will be found going in either
  direction).

  By default, a single path will be printed. If there is a path with only
  public dependencies, the shortest public path will be printed. Otherwise, the
  shortest path using either public or private dependencies will be printed. If
  --with-data is specified, data deps will also be considered. If there are
  multiple shortest paths, an arbitrary one will be selected.

Interesting paths

  In a large project, there can be 100's of millions of unique paths between a
  very high level and a common low-level target. To make the output more useful
  (and terminate in a reasonable time), GN will not revisit sub-paths
  previously known to lead to the target.

Options

  --all
     Prints all "interesting" paths found rather than just the first one.
     Public paths will be printed first in order of increasing length, followed
     by non-public paths in order of increasing length.

  --public
     Considers only public paths. Can't be used with --with-data.

  --with-data
     Additionally follows data deps. Without this flag, only public and private
     linked deps will be followed. Can't be used with --public.

Example

  gn path out/Default //base //tools/gn
)";

int RunPath(const std::vector<std::string>& args) {
  if (args.size() != 3) {
    Err(Location(), "You're holding it wrong.",
        "Usage: \"gn path <out_dir> <target_one> <target_two>\"")
        .PrintToStdout();
    return 1;
  }

  Setup* setup = new Setup;
  if (!setup->DoSetup(args[0], false))
    return 1;
  if (!setup->Run())
    return 1;

  const Target* target1 = ResolveTargetFromCommandLineString(setup, args[1]);
  if (!target1)
    return 1;
  const Target* target2 = ResolveTargetFromCommandLineString(setup, args[2]);
  if (!target2)
    return 1;

  Options options;
  options.print_what = base::CommandLine::ForCurrentProcess()->HasSwitch("all")
      ? PrintWhat::ALL : PrintWhat::ONE;
  options.public_only =
      base::CommandLine::ForCurrentProcess()->HasSwitch("public");
  options.with_data =
      base::CommandLine::ForCurrentProcess()->HasSwitch("with-data");
  if (options.public_only && options.with_data) {
    Err(Location(), "Can't use --public with --with-data for 'gn path'.",
        "Your zealous over-use of arguments has inevitably resulted in an "
        "invalid\ncombination of flags.").PrintToStdout();
    return 1;
  }

  Stats stats;
  DoSearch(target1, target2, options, &stats);
  if (stats.total_paths() == 0) {
    // If we don't find a path going "forwards", try the reverse direction.
    // Deps can only go in one direction without having a cycle, which will
    // have caused a run failure above.
    DoSearch(target2, target1, options, &stats);
  }

  // This string is inserted in the results to annotate whether the result
  // is only public or includes data deps or not.
  const char* path_annotation = "";
  if (options.public_only)
    path_annotation = "public ";
  else if (!options.with_data)
    path_annotation = "non-data ";

  if (stats.total_paths() == 0) {
    // No results.
    OutputString(base::StringPrintf(
        "No %spaths found between these two targets.\n", path_annotation),
        DECORATION_YELLOW);
  } else if (stats.total_paths() == 1) {
    // Exactly one result.
    OutputString(base::StringPrintf("1 %spath found.", path_annotation),
                 DECORATION_YELLOW);
    if (!options.public_only) {
      if (stats.public_paths)
        OutputString(" It is public.");
      else
        OutputString(" It is not public.");
    }
    OutputString("\n");
  } else {
    if (options.print_what == PrintWhat::ALL) {
      // Showing all paths when there are many.
      OutputString(base::StringPrintf("%d \"interesting\" %spaths found.",
                                      stats.total_paths(), path_annotation),
                   DECORATION_YELLOW);
      if (!options.public_only) {
        OutputString(base::StringPrintf(" %d of them are public.",
                                        stats.public_paths));
      }
      OutputString("\n");
    } else {
      // Showing one path when there are many.
      OutputString(
          base::StringPrintf("Showing one of %d \"interesting\" %spaths.",
                             stats.total_paths(), path_annotation),
          DECORATION_YELLOW);
      if (!options.public_only) {
        OutputString(
            base::StringPrintf(" %d of them are public.", stats.public_paths));
      }
      OutputString("\nUse --all to print all paths.\n");
    }
  }
  return 0;
}

}  // namespace commands
