// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_NINJA_BUILD_WRITER_H_
#define TOOLS_GN_NINJA_BUILD_WRITER_H_

#include <iosfwd>
#include <map>
#include <unordered_map>
#include <vector>

#include "base/macros.h"
#include "tools/gn/path_output.h"

class Builder;
class BuildSettings;
class Err;
class Settings;
class Target;
class Toolchain;

namespace base {
class CommandLine;
}  // base

// Generates the toplevel "build.ninja" file. This references the individual
// toolchain files and lists all input .gn files as dependencies of the
// build itself.
class NinjaBuildWriter {
 public:
  NinjaBuildWriter(const BuildSettings* settings,
                   const std::unordered_map<const Settings*, const Toolchain*>&
                       used_toolchains,
                   const std::vector<const Target*>& all_targets,
                   const Toolchain* default_toolchain,
                   const std::vector<const Target*>& default_toolchain_targets,
                   std::ostream& out,
                   std::ostream& dep_out);
  ~NinjaBuildWriter();

  // The design of this class is that this static factory function takes the
  // Builder, extracts the relevant information, and passes it to the class
  // constructor. The class itself doesn't depend on the Builder at all which
  // makes testing much easier (tests integrating various functions along with
  // the Builder get very complicated).
  static bool RunAndWriteFile(const BuildSettings* settings,
                              const Builder& builder,
                              Err* err);

  bool Run(Err* err);

 private:
  void WriteNinjaRules();
  void WriteAllPools();
  bool WriteSubninjas(Err* err);
  bool WritePhonyAndAllRules(Err* err);

  void WritePhonyRule(const Target* target, const std::string& phony_name);

  const BuildSettings* build_settings_;

  const std::unordered_map<const Settings*, const Toolchain*>& used_toolchains_;
  const std::vector<const Target*>& all_targets_;
  const Toolchain* default_toolchain_;
  const std::vector<const Target*>& default_toolchain_targets_;

  std::ostream& out_;
  std::ostream& dep_out_;
  PathOutput path_output_;

  DISALLOW_COPY_AND_ASSIGN(NinjaBuildWriter);
};

extern const char kNinjaRules_Help[];

// Exposed for testing.
base::CommandLine GetSelfInvocationCommandLine(
    const BuildSettings* build_settings);

#endif  // TOOLS_GN_NINJA_BUILD_WRITER_H_
