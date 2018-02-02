// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/header_checker.h"

#include <algorithm>

#include "base/bind.h"
#include "base/containers/queue.h"
#include "base/files/file_util.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "tools/gn/build_settings.h"
#include "tools/gn/builder.h"
#include "tools/gn/c_include_iterator.h"
#include "tools/gn/config.h"
#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/source_file_type.h"
#include "tools/gn/target.h"
#include "tools/gn/trace.h"

namespace {

struct PublicGeneratedPair {
  PublicGeneratedPair() : is_public(false), is_generated(false) {}
  bool is_public;
  bool is_generated;
};

// If the given file is in the "gen" folder, trims this so it treats the gen
// directory as the source root:
//   //out/Debug/gen/foo/bar.h -> //foo/bar.h
// If the file isn't in the generated root, returns the input unchanged.
SourceFile RemoveRootGenDirFromFile(const Target* target,
                                    const SourceFile& file) {
  const SourceDir& gen = target->settings()->toolchain_gen_dir();
  if (!gen.is_null() && base::StartsWith(file.value(), gen.value(),
                                         base::CompareCase::SENSITIVE))
    return SourceFile("//" + file.value().substr(gen.value().size()));
  return file;
}

// This class makes InputFiles on the stack as it reads files to check. When
// we throw an error, the Err indicates a locatin which has a pointer to
// an InputFile that must persist as long as the Err does.
//
// To make this work, this function creates a clone of the InputFile managed
// by the InputFileManager so the error can refer to something that
// persists. This means that the current file contents will live as long as
// the program, but this is OK since we're erroring out anyway.
LocationRange CreatePersistentRange(const InputFile& input_file,
                                    const LocationRange& range) {
  InputFile* clone_input_file;
  std::vector<Token>* tokens;  // Don't care about this.
  std::unique_ptr<ParseNode>* parse_root;  // Don't care about this.

  g_scheduler->input_file_manager()->AddDynamicInput(
      input_file.name(), &clone_input_file, &tokens, &parse_root);
  clone_input_file->SetContents(input_file.contents());

  return LocationRange(Location(clone_input_file,
                                range.begin().line_number(),
                                range.begin().column_number(),
                                -1 /* TODO(scottmg) */),
                       Location(clone_input_file,
                                range.end().line_number(),
                                range.end().column_number(),
                                -1 /* TODO(scottmg) */));
}

// Given a reverse dependency chain where the target chain[0]'s includes are
// being used by chain[end] and not all deps are public, returns the string
// describing the error.
std::string GetDependencyChainPublicError(
    const HeaderChecker::Chain& chain) {
  std::string ret = "The target:\n  " +
      chain[chain.size() - 1].target->label().GetUserVisibleName(false) +
      "\nis including a file from the target:\n  " +
      chain[0].target->label().GetUserVisibleName(false) +
      "\n";

  // Invalid chains should always be 0 (no chain) or more than two
  // (intermediate private dependencies). 1 and 2 are impossible because a
  // target can always include headers from itself and its direct dependents.
  DCHECK(chain.size() != 1 && chain.size() != 2);
  if (chain.empty()) {
    ret += "There is no dependency chain between these targets.";
  } else {
    // Indirect dependency chain, print the chain.
    ret += "\nIt's usually best to depend directly on the destination target.\n"
        "In some cases, the destination target is considered a subcomponent\n"
        "of an intermediate target. In this case, the intermediate target\n"
        "should depend publicly on the destination to forward the ability\n"
        "to include headers.\n"
        "\n"
        "Dependency chain (there may also be others):\n";

    for (int i = static_cast<int>(chain.size()) - 1; i >= 0; i--) {
      ret.append("  " + chain[i].target->label().GetUserVisibleName(false));
      if (i != 0) {
        // Identify private dependencies so the user can see where in the
        // dependency chain things went bad. Don't list this for the first link
        // in the chain since direct dependencies are OK, and listing that as
        // "private" may make people feel like they need to fix it.
        if (i == static_cast<int>(chain.size()) - 1 || chain[i - 1].is_public)
          ret.append(" -->");
        else
          ret.append(" --[private]-->");
      }
      ret.append("\n");
    }
  }
  return ret;
}

// Returns true if the two targets have the same label not counting the
// toolchain.
bool TargetLabelsMatchExceptToolchain(const Target* a, const Target* b) {
  return a->label().dir() == b->label().dir() &&
         a->label().name() == b->label().name();
}

}  // namespace

HeaderChecker::HeaderChecker(const BuildSettings* build_settings,
                             const std::vector<const Target*>& targets)
    : main_loop_(base::MessageLoop::current()),
      build_settings_(build_settings) {
  for (auto* target : targets)
    AddTargetToFileMap(target, &file_map_);
}

HeaderChecker::~HeaderChecker() = default;

bool HeaderChecker::Run(const std::vector<const Target*>& to_check,
                        bool force_check,
                        std::vector<Err>* errors) {
  FileMap files_to_check;
  for (auto* check : to_check)
    AddTargetToFileMap(check, &files_to_check);
  RunCheckOverFiles(files_to_check, force_check);

  if (errors_.empty())
    return true;
  *errors = errors_;
  return false;
}

void HeaderChecker::RunCheckOverFiles(const FileMap& files, bool force_check) {
  if (files.empty())
    return;

  scoped_refptr<base::SequencedWorkerPool> pool(new base::SequencedWorkerPool(
      16, "HeaderChecker", base::TaskPriority::USER_VISIBLE));
  for (const auto& file : files) {
    // Only check C-like source files (RC files also have includes).
    SourceFileType type = GetSourceFileType(file.first);
    if (type != SOURCE_CPP && type != SOURCE_H && type != SOURCE_C &&
        type != SOURCE_M && type != SOURCE_MM && type != SOURCE_RC)
      continue;

    // If any target marks it as generated, don't check it. We have to check
    // file_map_, which includes all known files; files only includes those
    // being checked.
    bool is_generated = false;
    for (const auto& vect_i : file_map_[file.first])
      is_generated |= vect_i.is_generated;
    if (is_generated)
      continue;

    for (const auto& vect_i : file.second) {
      if (vect_i.target->check_includes()) {
        pool->PostWorkerTaskWithShutdownBehavior(
            FROM_HERE,
            base::Bind(&HeaderChecker::DoWork, this, vect_i.target, file.first),
            base::SequencedWorkerPool::BLOCK_SHUTDOWN);
      }
    }
  }

  // After this call we're single-threaded again.
  pool->Shutdown();
}

void HeaderChecker::DoWork(const Target* target, const SourceFile& file) {
  Err err;
  if (!CheckFile(target, file, &err)) {
    base::AutoLock lock(lock_);
    errors_.push_back(err);
  }
}

// static
void HeaderChecker::AddTargetToFileMap(const Target* target, FileMap* dest) {
  // Files in the sources have this public bit by default.
  bool default_public = target->all_headers_public();

  std::map<SourceFile, PublicGeneratedPair> files_to_public;

  // First collect the normal files, they get the default visibility. Always
  // trim the root gen dir if it exists. This will only exist on outputs of an
  // action, but those are often then wired into the sources of a compiled
  // target to actually compile generated code. If you depend on the compiled
  // target, it should be enough to be able to include the header.
  for (const auto& source : target->sources()) {
    SourceFile file = RemoveRootGenDirFromFile(target, source);
    files_to_public[file].is_public = default_public;
  }

  // Add in the public files, forcing them to public. This may overwrite some
  // entries, and it may add new ones.
  if (default_public)  // List only used when default is not public.
    DCHECK(target->public_headers().empty());
  for (const auto& source : target->public_headers()) {
    SourceFile file = RemoveRootGenDirFromFile(target, source);
    files_to_public[file].is_public = true;
  }

  // Add in outputs from actions. These are treated as public (since if other
  // targets can't use them, then there wouldn't be any point in outputting).
  std::vector<SourceFile> outputs;
  target->action_values().GetOutputsAsSourceFiles(target, &outputs);
  for (const auto& output : outputs) {
    // For generated files in the "gen" directory, add the filename to the
    // map assuming "gen" is the source root. This means that when files include
    // the generated header relative to there (the recommended practice), we'll
    // find the file.
    SourceFile output_file = RemoveRootGenDirFromFile(target, output);
    PublicGeneratedPair* pair = &files_to_public[output_file];
    pair->is_public = true;
    pair->is_generated = true;
  }

  // Add the merged list to the master list of all files.
  for (const auto& cur : files_to_public) {
    (*dest)[cur.first].push_back(TargetInfo(
        target, cur.second.is_public, cur.second.is_generated));
  }
}

bool HeaderChecker::IsFileInOuputDir(const SourceFile& file) const {
  const std::string& build_dir = build_settings_->build_dir().value();
  return file.value().compare(0, build_dir.size(), build_dir) == 0;
}

// This current assumes all include paths are relative to the source root
// which is generally the case for Chromium.
//
// A future enhancement would be to search the include path for the target
// containing the source file containing this include and find the file to
// handle the cases where people do weird things with the paths.
SourceFile HeaderChecker::SourceFileForInclude(
    const base::StringPiece& input) const {
  std::string str("//");
  input.AppendToString(&str);
  return SourceFile(str);
}

bool HeaderChecker::CheckFile(const Target* from_target,
                              const SourceFile& file,
                              Err* err) const {
  ScopedTrace trace(TraceItem::TRACE_CHECK_HEADER, file.value());

  // Sometimes you have generated source files included as sources in another
  // target. These won't exist at checking time. Since we require all generated
  // files to be somewhere in the output tree, we can just check the name to
  // see if they should be skipped.
  if (IsFileInOuputDir(file))
    return true;

  base::FilePath path = build_settings_->GetFullPath(file);
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    *err = Err(from_target->defined_from(), "Source file not found.",
        "The target:\n  " + from_target->label().GetUserVisibleName(false) +
        "\nhas a source file:\n  " + file.value() +
        "\nwhich was not found.");
    return false;
  }

  InputFile input_file(file);
  input_file.SetContents(contents);

  CIncludeIterator iter(&input_file);
  base::StringPiece current_include;
  LocationRange range;
  while (iter.GetNextIncludeString(&current_include, &range)) {
    SourceFile include = SourceFileForInclude(current_include);
    if (!CheckInclude(from_target, input_file, include, range, err))
      return false;
  }

  return true;
}

// If the file exists:
//  - The header must be in the public section of a target, or it must
//    be in the sources with no public list (everything is implicitly public).
//  - The dependency path to the included target must follow only public_deps.
//  - If there are multiple targets with the header in it, only one need be
//    valid for the check to pass.
bool HeaderChecker::CheckInclude(const Target* from_target,
                                 const InputFile& source_file,
                                 const SourceFile& include_file,
                                 const LocationRange& range,
                                 Err* err) const {
  // Assume if the file isn't declared in our sources that we don't need to
  // check it. It would be nice if we could give an error if this happens, but
  // our include finder is too primitive and returns all includes, even if
  // they're in a #if not executed in the current build. In that case, it's
  // not unusual for the buildfiles to not specify that header at all.
  FileMap::const_iterator found = file_map_.find(include_file);
  if (found == file_map_.end())
    return true;

  const TargetVector& targets = found->second;
  Chain chain;  // Prevent reallocating in the loop.

  // If the file is unknown in the current toolchain (rather than being private
  // or in a target not visible to the current target), ignore it. This is a
  // bit of a hack to account for the fact that the include finder doesn't
  // understand the preprocessor.
  //
  // When not cross-compiling, if a platform specific header is conditionally
  // included in the build, and preprocessor conditions around #includes of
  // that match the build conditions, everything will be OK because the file
  // won't be known to GN even though the #include finder identified the file.
  //
  // Cross-compiling breaks this. When compiling Android on Linux, for example,
  // we might see both Linux and Android definitions of a target and know
  // about the union of all headers in the build. Since the #include finder
  // ignores preprocessor, we will find the Linux headers in the Android
  // build and note that a dependency from the Android target to the Linux
  // one is missing (these might even be the same target in different
  // toolchains!).
  bool present_in_current_toolchain = false;
  for (const auto& target : targets) {
    if (from_target->label().ToolchainsEqual(target.target->label())) {
      present_in_current_toolchain = true;
      break;
    }
  }
  if (!present_in_current_toolchain)
    return true;

  // For all targets containing this file, we require that at least one be
  // a direct or public dependency of the current target, and that the header
  // is public within the target.
  //
  // If there is more than one target containing this header, we may encounter
  // some error cases before finding a good one. This error stores the previous
  // one encountered, which we may or may not throw away.
  Err last_error;

  bool found_dependency = false;
  for (const auto& target : targets) {
    // We always allow source files in a target to include headers also in that
    // target.
    const Target* to_target = target.target;
    if (to_target == from_target)
      return true;

    bool is_permitted_chain = false;
    if (IsDependencyOf(to_target, from_target, &chain, &is_permitted_chain)) {
      DCHECK(chain.size() >= 2);
      DCHECK(chain[0].target == to_target);
      DCHECK(chain[chain.size() - 1].target == from_target);

      found_dependency = true;

      if (target.is_public && is_permitted_chain) {
        // This one is OK, we're done.
        last_error = Err();
        break;
      }

      // Diagnose the error.
      if (!target.is_public) {
        // Danger: must call CreatePersistentRange to put in Err.
        last_error = Err(CreatePersistentRange(source_file, range),
                         "Including a private header.",
                         "This file is private to the target " +
                             target.target->label().GetUserVisibleName(false));
      } else if (!is_permitted_chain) {
        last_error = Err(
            CreatePersistentRange(source_file, range),
            "Can't include this header from here.",
                GetDependencyChainPublicError(chain));
      } else {
        NOTREACHED();
      }
    } else if (
        to_target->allow_circular_includes_from().find(from_target->label()) !=
        to_target->allow_circular_includes_from().end()) {
      // Not a dependency, but this include is whitelisted from the destination.
      found_dependency = true;
      last_error = Err();
      break;
    }
  }

  if (!found_dependency) {
    DCHECK(!last_error.has_error());
    *err = MakeUnreachableError(source_file, range, from_target, targets);
    return false;
  }
  if (last_error.has_error()) {
    // Found at least one dependency chain above, but it had an error.
    *err = last_error;
    return false;
  }

  // One thing we didn't check for is targets that expose their dependents
  // headers in their own public headers.
  //
  // Say we have A -> B -> C. If C has public_configs, everybody getting headers
  // from C should get the configs also or things could be out-of-sync. Above,
  // we check for A including C's headers directly, but A could also include a
  // header from B that in turn includes a header from C.
  //
  // There are two ways to solve this:
  //  - If a public header in B includes C, force B to publicly depend on C.
  //    This is possible to check, but might be super annoying because most
  //    targets (especially large leaf-node targets) don't declare
  //    public/private headers and you'll get lots of false positives.
  //
  //  - Save the includes found in each file and actually compute the graph of
  //    includes to detect when A implicitly includes C's header. This will not
  //    have the annoying false positive problem, but is complex to write.

  return true;
}

bool HeaderChecker::IsDependencyOf(const Target* search_for,
                                   const Target* search_from,
                                   Chain* chain,
                                   bool* is_permitted) const {
  if (search_for == search_from) {
    // A target is always visible from itself.
    *is_permitted = true;
    return false;
  }

  // Find the shortest public dependency chain.
  if (IsDependencyOf(search_for, search_from, true, chain)) {
    *is_permitted = true;
    return true;
  }

  // If not, try to find any dependency chain at all.
  if (IsDependencyOf(search_for, search_from, false, chain)) {
    *is_permitted = false;
    return true;
  }

  *is_permitted = false;
  return false;
}

bool HeaderChecker::IsDependencyOf(const Target* search_for,
                                   const Target* search_from,
                                   bool require_permitted,
                                   Chain* chain) const {
  // This method conducts a breadth-first search through the dependency graph
  // to find a shortest chain from search_from to search_for.
  //
  // work_queue maintains a queue of targets which need to be considered as
  // part of this chain, in the order they were first traversed.
  //
  // Each time a new transitive dependency of search_from is discovered for
  // the first time, it is added to work_queue and a "breadcrumb" is added,
  // indicating which target it was reached from when first discovered.
  //
  // Once this search finds search_for, the breadcrumbs are used to reconstruct
  // a shortest dependency chain (in reverse order) from search_from to
  // search_for.

  std::map<const Target*, ChainLink> breadcrumbs;
  base::queue<ChainLink> work_queue;
  work_queue.push(ChainLink(search_from, true));

  bool first_time = true;
  while (!work_queue.empty()) {
    ChainLink cur_link = work_queue.front();
    const Target* target = cur_link.target;
    work_queue.pop();

    if (target == search_for) {
      // Found it! Reconstruct the chain.
      chain->clear();
      while (target != search_from) {
        chain->push_back(cur_link);
        cur_link = breadcrumbs[target];
        target = cur_link.target;
      }
      chain->push_back(ChainLink(search_from, true));
      return true;
    }

    // Always consider public dependencies as possibilities.
    for (const auto& dep : target->public_deps()) {
      if (breadcrumbs.insert(std::make_pair(dep.ptr, cur_link)).second)
        work_queue.push(ChainLink(dep.ptr, true));
    }

    if (first_time || !require_permitted) {
      // Consider all dependencies since all target paths are allowed, so add
      // in private ones. Also do this the first time through the loop, since
      // a target can include headers from its direct deps regardless of
      // public/private-ness.
      first_time = false;
      for (const auto& dep : target->private_deps()) {
        if (breadcrumbs.insert(std::make_pair(dep.ptr, cur_link)).second)
          work_queue.push(ChainLink(dep.ptr, false));
      }
    }
  }

  return false;
}

Err HeaderChecker::MakeUnreachableError(
    const InputFile& source_file,
    const LocationRange& range,
    const Target* from_target,
    const TargetVector& targets) {
  // Normally the toolchains will all match, but when cross-compiling, we can
  // get targets with more than one toolchain in the list of possibilities.
  std::vector<const Target*> targets_with_matching_toolchains;
  std::vector<const Target*> targets_with_other_toolchains;
  for (const TargetInfo& candidate : targets) {
    if (candidate.target->toolchain() == from_target->toolchain())
      targets_with_matching_toolchains.push_back(candidate.target);
    else
      targets_with_other_toolchains.push_back(candidate.target);
  }

  // It's common when cross-compiling to have a target with the same file in
  // more than one toolchain. We could output all of them, but this is
  // generally confusing to people (most end-users won't understand toolchains
  // well).
  //
  // So delete any candidates in other toolchains that also appear in the same
  // toolchain as the from_target.
  for (int other_index = 0;
       other_index < static_cast<int>(targets_with_other_toolchains.size());
       other_index++) {
    for (const Target* cur_matching : targets_with_matching_toolchains) {
      if (TargetLabelsMatchExceptToolchain(
              cur_matching, targets_with_other_toolchains[other_index])) {
        // Found a duplicate, erase it.
        targets_with_other_toolchains.erase(
            targets_with_other_toolchains.begin() + other_index);
        other_index--;
        break;
      }
    }
  }

  // Only display toolchains on labels if they don't all match.
  bool include_toolchain = !targets_with_other_toolchains.empty();

  std::string msg = "It is not in any dependency of\n  " +
      from_target->label().GetUserVisibleName(include_toolchain);
  msg += "\nThe include file is in the target(s):\n";
  for (auto* target : targets_with_matching_toolchains)
    msg += "  " + target->label().GetUserVisibleName(include_toolchain) + "\n";
  for (auto* target : targets_with_other_toolchains)
    msg += "  " + target->label().GetUserVisibleName(include_toolchain) + "\n";
  if (targets_with_other_toolchains.size() +
      targets_with_matching_toolchains.size() > 1)
    msg += "at least one of ";
  msg += "which should somehow be reachable.";

  // Danger: must call CreatePersistentRange to put in Err.
  return Err(CreatePersistentRange(source_file, range),
             "Include not allowed.", msg);
}

