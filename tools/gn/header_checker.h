// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_HEADER_CHECKER_H_
#define TOOLS_GN_HEADER_CHECKER_H_

#include <map>
#include <set>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/strings/string_piece.h"
#include "base/synchronization/lock.h"
#include "tools/gn/err.h"

class BuildSettings;
class InputFile;
class LocationRange;
class SourceFile;
class Target;

namespace base {
class MessageLoop;
}

class HeaderChecker : public base::RefCountedThreadSafe<HeaderChecker> {
 public:
  // Represents a dependency chain.
  struct ChainLink {
    ChainLink() : target(nullptr), is_public(false) {}
    ChainLink(const Target* t, bool p) : target(t), is_public(p) {}

    const Target* target;

    // True when the dependency on this target is public.
    bool is_public;

    // Used for testing.
    bool operator==(const ChainLink& other) const {
      return target == other.target && is_public == other.is_public;
    }
  };
  typedef std::vector<ChainLink> Chain;

  HeaderChecker(const BuildSettings* build_settings,
                const std::vector<const Target*>& targets);

  // Runs the check. The targets in to_check will be checked.
  //
  // This assumes that the current thread already has a message loop. On
  // error, fills the given vector with the errors and returns false. Returns
  // true on success.
  //
  // force_check, if true, will override targets opting out of header checking
  // with "check_includes = false" and will check them anyway.
  bool Run(const std::vector<const Target*>& to_check,
           bool force_check,
           std::vector<Err>* errors);

 private:
  friend class base::RefCountedThreadSafe<HeaderChecker>;
  FRIEND_TEST_ALL_PREFIXES(HeaderCheckerTest, IsDependencyOf);
  FRIEND_TEST_ALL_PREFIXES(HeaderCheckerTest, CheckInclude);
  FRIEND_TEST_ALL_PREFIXES(HeaderCheckerTest, PublicFirst);
  FRIEND_TEST_ALL_PREFIXES(HeaderCheckerTest, CheckIncludeAllowCircular);
  ~HeaderChecker();

  struct TargetInfo {
    TargetInfo() : target(nullptr), is_public(false), is_generated(false) {}
    TargetInfo(const Target* t, bool is_pub, bool is_gen)
        : target(t),
          is_public(is_pub),
          is_generated(is_gen) {
    }

    const Target* target;

    // True if the file is public in the given target.
    bool is_public;

    // True if this file is generated and won't actually exist on disk.
    bool is_generated;
  };

  typedef std::vector<TargetInfo> TargetVector;
  typedef std::map<SourceFile, TargetVector> FileMap;

  // Backend for Run() that takes the list of files to check. The errors_ list
  // will be populate on failure.
  void RunCheckOverFiles(const FileMap& flies, bool force_check);

  void DoWork(const Target* target, const SourceFile& file);

  // Adds the sources and public files from the given target to the given map.
  static void AddTargetToFileMap(const Target* target, FileMap* dest);

  // Returns true if the given file is in the output directory.
  bool IsFileInOuputDir(const SourceFile& file) const;

  // Resolves the contents of an include to a SourceFile.
  SourceFile SourceFileForInclude(const base::StringPiece& input) const;

  // from_target is the target the file was defined from. It will be used in
  // error messages.
  bool CheckFile(const Target* from_target,
                 const SourceFile& file,
                 Err* err) const;

  // Checks that the given file in the given target can include the given
  // include file. If disallowed, returns false and sets the error. The
  // range indicates the location of the include in the file for error
  // reporting.
  bool CheckInclude(const Target* from_target,
                    const InputFile& source_file,
                    const SourceFile& include_file,
                    const LocationRange& range,
                    Err* err) const;

  // Returns true if the given search_for target is a dependency of
  // search_from.
  //
  // If found, the vector given in "chain" will be filled with the reverse
  // dependency chain from the dest target (chain[0] = search_for) to the src
  // target (chain[chain.size() - 1] = search_from).
  //
  // Chains with permitted dependencies will be considered first. If a
  // permitted match is found, *is_permitted will be set to true. A chain with
  // indirect, non-public dependencies will only be considered if there are no
  // public or direct chains. In this case, *is_permitted will be false.
  //
  // A permitted dependency is a sequence of public dependencies. The first
  // one may be private, since a direct dependency always allows headers to be
  // included.
  bool IsDependencyOf(const Target* search_for,
                      const Target* search_from,
                      Chain* chain,
                      bool* is_permitted) const;

  // For internal use by the previous override of IsDependencyOf.  If
  // require_public is true, only public dependency chains are searched.
  bool IsDependencyOf(const Target* search_for,
                      const Target* search_from,
                      bool require_permitted,
                      Chain* chain) const;

  // Makes a very descriptive error message for when an include is disallowed
  // from a given from_target, with a missing dependency to one of the given
  // targets.
  static Err MakeUnreachableError(const InputFile& source_file,
                                  const LocationRange& range,
                                  const Target* from_target,
                                  const TargetVector& targets);

  // Non-locked variables ------------------------------------------------------
  //
  // These are initialized during construction (which happens on one thread)
  // and are not modified after, so any thread can read these without locking.

  base::MessageLoop* main_loop_;
  base::RunLoop main_thread_runner_;

  const BuildSettings* build_settings_;

  // Maps source files to targets it appears in (usually just one target).
  FileMap file_map_;

  // Locked variables ----------------------------------------------------------
  //
  // These are mutable during runtime and require locking.

  base::Lock lock_;

  std::vector<Err> errors_;

  DISALLOW_COPY_AND_ASSIGN(HeaderChecker);
};

#endif  // TOOLS_GN_HEADER_CHECKER_H_
