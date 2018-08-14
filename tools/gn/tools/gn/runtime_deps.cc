// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/runtime_deps.h"

#include <map>
#include <set>
#include <sstream>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "tools/gn/build_settings.h"
#include "tools/gn/builder.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/loader.h"
#include "tools/gn/output_file.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/settings.h"
#include "tools/gn/switches.h"
#include "tools/gn/target.h"
#include "tools/gn/trace.h"

namespace {

using RuntimeDepsVector = std::vector<std::pair<OutputFile, const Target*>>;

// Adds the given file to the deps list if it hasn't already been listed in
// the found_files list. Updates the list.
void AddIfNew(const OutputFile& output_file,
              const Target* source,
              RuntimeDepsVector* deps,
              std::set<OutputFile>* found_file) {
  if (found_file->find(output_file) != found_file->end())
    return;  // Already there.
  deps->push_back(std::make_pair(output_file, source));
}

// Automatically converts a string that looks like a source to an OutputFile.
void AddIfNew(const std::string& str,
              const Target* source,
              RuntimeDepsVector* deps,
              std::set<OutputFile>* found_file) {
  OutputFile output_file(
      RebasePath(str, source->settings()->build_settings()->build_dir(),
                 source->settings()->build_settings()->root_path_utf8()));
  AddIfNew(output_file, source, deps, found_file);
}

// To avoid duplicate traversals of targets, or duplicating output files that
// might be listed by more than one target, the set of targets and output files
// that have been found so far is passed. The "value" of the seen_targets map
// is a boolean indicating if the seen dep was a data dep (true = data_dep).
// data deps add more stuff, so we will want to revisit a target if it's a
// data dependency and we've previously only seen it as a regular dep.
void RecursiveCollectRuntimeDeps(const Target* target,
                                 bool is_target_data_dep,
                                 RuntimeDepsVector* deps,
                                 std::map<const Target*, bool>* seen_targets,
                                 std::set<OutputFile>* found_files) {
  const auto& found_seen_target = seen_targets->find(target);
  if (found_seen_target != seen_targets->end()) {
    // Already visited.
    if (found_seen_target->second || !is_target_data_dep) {
      // Already visited as a data dep, or the current dep is not a data
      // dep so visiting again will be a no-op.
      return;
    }
    // In the else case, the previously seen target was a regular dependency
    // and we'll now process it as a data dependency.
  }
  (*seen_targets)[target] = is_target_data_dep;

  // Add the main output file for executables, shared libraries, and
  // loadable modules.
  if (target->output_type() == Target::EXECUTABLE ||
      target->output_type() == Target::LOADABLE_MODULE ||
      target->output_type() == Target::SHARED_LIBRARY) {
    for (const auto& runtime_output : target->runtime_outputs())
      AddIfNew(runtime_output, target, deps, found_files);
  }

  // Add all data files.
  for (const auto& file : target->data())
    AddIfNew(file, target, deps, found_files);

  // Actions/copy have all outputs considered when the're a data dep.
  if (is_target_data_dep && (target->output_type() == Target::ACTION ||
                             target->output_type() == Target::ACTION_FOREACH ||
                             target->output_type() == Target::COPY_FILES)) {
    std::vector<SourceFile> outputs;
    target->action_values().GetOutputsAsSourceFiles(target, &outputs);
    for (const auto& output_file : outputs)
      AddIfNew(output_file.value(), target, deps, found_files);
  }

  // Data dependencies.
  for (const auto& dep_pair : target->data_deps()) {
    RecursiveCollectRuntimeDeps(dep_pair.ptr, true, deps, seen_targets,
                                found_files);
  }

  // Do not recurse into bundle targets. A bundle's dependencies should be
  // copied into the bundle itself for run-time access.
  if (target->output_type() == Target::CREATE_BUNDLE) {
    SourceDir bundle_root_dir =
        target->bundle_data().GetBundleRootDirOutputAsDir(target->settings());
    AddIfNew(bundle_root_dir.value(), target, deps, found_files);
    return;
  }

  // Non-data dependencies (both public and private).
  for (const auto& dep_pair : target->GetDeps(Target::DEPS_LINKED)) {
    if (dep_pair.ptr->output_type() == Target::EXECUTABLE)
      continue;  // Skip executables that aren't data deps.
    if (dep_pair.ptr->output_type() == Target::SHARED_LIBRARY &&
        (target->output_type() == Target::ACTION ||
         target->output_type() == Target::ACTION_FOREACH)) {
      // Skip shared libraries that action depends on,
      // unless it were listed in data deps.
      continue;
    }
    RecursiveCollectRuntimeDeps(dep_pair.ptr, false, deps, seen_targets,
                                found_files);
  }
}

bool CollectRuntimeDepsFromFlag(const Builder& builder,
                                RuntimeDepsVector* files_to_write,
                                Err* err) {
  std::string deps_target_list_file =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kRuntimeDepsListFile);

  if (deps_target_list_file.empty())
    return true;

  std::string list_contents;
  ScopedTrace load_trace(TraceItem::TRACE_FILE_LOAD, deps_target_list_file);
  if (!base::ReadFileToString(UTF8ToFilePath(deps_target_list_file),
                              &list_contents)) {
    *err = Err(Location(),
               std::string("File for --") + switches::kRuntimeDepsListFile +
                   " doesn't exist.",
               "The file given was \"" + deps_target_list_file + "\"");
    return false;
  }
  load_trace.Done();

  SourceDir root_dir("//");
  Label default_toolchain_label = builder.loader()->GetDefaultToolchain();
  for (const auto& line : base::SplitString(
           list_contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (line.empty())
      continue;
    Label label = Label::Resolve(root_dir, default_toolchain_label,
                                 Value(nullptr, line), err);
    if (err->has_error())
      return false;

    const Item* item = builder.GetItem(label);
    const Target* target = item ? item->AsTarget() : nullptr;
    if (!target) {
      *err =
          Err(Location(),
              "The label \"" + label.GetUserVisibleName(true) +
                  "\" isn't a target.",
              "When reading the line:\n  " + line +
                  "\n"
                  "from the --" +
                  switches::kRuntimeDepsListFile + "=" + deps_target_list_file);
      return false;
    }

    OutputFile output_file;
    const char extension[] = ".runtime_deps";
    if (target->output_type() == Target::SHARED_LIBRARY ||
        target->output_type() == Target::LOADABLE_MODULE) {
      // Force the first output for shared-library-type linker outputs since
      // the dependency output files might not be the main output.
      CHECK(!target->computed_outputs().empty());
      output_file =
          OutputFile(target->computed_outputs()[0].value() + extension);
    } else {
      output_file =
          OutputFile(target->dependency_output_file().value() + extension);
    }
    files_to_write->push_back(std::make_pair(output_file, target));
  }
  return true;
}

bool WriteRuntimeDepsFile(const OutputFile& output_file,
                          const Target* target,
                          Err* err) {
  SourceFile output_as_source =
      output_file.AsSourceFile(target->settings()->build_settings());
  base::FilePath data_deps_file =
      target->settings()->build_settings()->GetFullPath(output_as_source);

  std::stringstream contents;
  for (const auto& pair : ComputeRuntimeDeps(target))
    contents << pair.first.value() << std::endl;

  ScopedTrace trace(TraceItem::TRACE_FILE_WRITE, output_as_source.value());
  return WriteFileIfChanged(data_deps_file, contents.str(), err);
}

}  // namespace

const char kRuntimeDeps_Help[] =
    R"(Runtime dependencies

  Runtime dependencies of a target are exposed via the "runtime_deps" category
  of "gn desc" (see "gn help desc") or they can be written at build generation
  time via write_runtime_deps(), or --runtime-deps-list-file (see "gn help
  --runtime-deps-list-file").

  To a first approximation, the runtime dependencies of a target are the set of
  "data" files, data directories, and the shared libraries from all transitive
  dependencies. Executables, shared libraries, and loadable modules are
  considered runtime dependencies of themselves.

Executables

  Executable targets and those executable targets' transitive dependencies are
  not considered unless that executable is listed in "data_deps". Otherwise, GN
  assumes that the executable (and everything it requires) is a build-time
  dependency only.

Actions and copies

  Action and copy targets that are listed as "data_deps" will have all of their
  outputs and data files considered as runtime dependencies. Action and copy
  targets that are "deps" or "public_deps" will have only their data files
  considered as runtime dependencies. These targets can list an output file in
  both the "outputs" and "data" lists to force an output file as a runtime
  dependency in all cases.

  The different rules for deps and data_deps are to express build-time (deps)
  vs. run-time (data_deps) outputs. If GN counted all build-time copy steps as
  data dependencies, there would be a lot of extra stuff, and if GN counted all
  run-time dependencies as regular deps, the build's parallelism would be
  unnecessarily constrained.

  This rule can sometimes lead to unintuitive results. For example, given the
  three targets:
    A  --[data_deps]-->  B  --[deps]-->  ACTION
  GN would say that A does not have runtime deps on the result of the ACTION,
  which is often correct. But the purpose of the B target might be to collect
  many actions into one logic unit, and the "data"-ness of A's dependency is
  lost. Solutions:

   - List the outputs of the action in its data section (if the results of
     that action are always runtime files).
   - Have B list the action in data_deps (if the outputs of the actions are
     always runtime files).
   - Have B list the action in both deps and data deps (if the outputs might be
     used in both contexts and you don't care about unnecessary entries in the
     list of files required at runtime).
   - Split B into run-time and build-time versions with the appropriate "deps"
     for each.

Static libraries and source sets

  The results of static_library or source_set targets are not considered
  runtime dependencies since these are assumed to be intermediate targets only.
  If you need to list a static library as a runtime dependency, you can
  manually compute the .a/.lib file name for the current platform and list it
  in the "data" list of a target (possibly on the static library target
  itself).

Multiple outputs

  Linker tools can specify which of their outputs should be considered when
  computing the runtime deps by setting runtime_outputs. If this is unset on
  the tool, the default will be the first output only.
)";

RuntimeDepsVector ComputeRuntimeDeps(const Target* target) {
  RuntimeDepsVector result;
  std::map<const Target*, bool> seen_targets;
  std::set<OutputFile> found_files;

  // The initial target is not considered a data dependency so that actions's
  // outputs (if the current target is an action) are not automatically
  // considered data deps.
  RecursiveCollectRuntimeDeps(target, false, &result, &seen_targets,
                              &found_files);
  return result;
}

bool WriteRuntimeDepsFilesIfNecessary(const Builder& builder, Err* err) {
  RuntimeDepsVector files_to_write;
  if (!CollectRuntimeDepsFromFlag(builder, &files_to_write, err))
    return false;

  // Files scheduled by write_runtime_deps.
  for (const Target* target : g_scheduler->GetWriteRuntimeDepsTargets()) {
    files_to_write.push_back(
        std::make_pair(target->write_runtime_deps_output(), target));
  }

  for (const auto& entry : files_to_write) {
    // Currently this writes all runtime deps files sequentially. We generally
    // expect few of these. We can run this on the worker pool if it looks
    // like it's talking a long time.
    if (!WriteRuntimeDepsFile(entry.first, entry.second, err))
      return false;
  }
  return true;
}
