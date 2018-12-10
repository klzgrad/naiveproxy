// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_ECLIPSE_WRITER_H_
#define TOOLS_GN_ECLIPSE_WRITER_H_

#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/macros.h"

class BuildSettings;
class Builder;
class Err;
class Target;

class EclipseWriter {
 public:
  static bool RunAndWriteFile(const BuildSettings* build_settings,
                              const Builder& builder,
                              Err* err);

 private:
  EclipseWriter(const BuildSettings* build_settings,
                const Builder& builder,
                std::ostream& out);
  ~EclipseWriter();

  void Run();

  // Populates |include_dirs_| with the include dirs of all the targets for the
  // default toolchain.
  void GetAllIncludeDirs();

  // Populates |defines_| with the defines of all the targets for the default
  // toolchain.
  void GetAllDefines();

  // Returns true if |target| uses the default toolchain.
  bool UsesDefaultToolchain(const Target* target) const;

  // Writes the XML settings file.
  void WriteCDTSettings();

  const BuildSettings* build_settings_;
  const Builder& builder_;

  // The output stream for the settings file.
  std::ostream& out_;

  // Eclipse languages for which the include dirs and defines apply.
  std::vector<std::string> languages_;

  // The include dirs of all the targets which use the default toolchain.
  std::set<std::string> include_dirs_;

  // The defines of all the targets which use the default toolchain.
  std::map<std::string, std::string> defines_;

  DISALLOW_COPY_AND_ASSIGN(EclipseWriter);
};

#endif  // TOOLS_GN_ECLIPSE_WRITER_H_
