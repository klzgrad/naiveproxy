// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_XCODE_WRITER_H_
#define TOOLS_GN_XCODE_WRITER_H_

#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"

class Builder;
class BuildSettings;
class Err;
class Target;

using PBXAttributes = std::map<std::string, std::string>;
class PBXProject;

class XcodeWriter {
 public:
  enum TargetOsType {
    WRITER_TARGET_OS_IOS,
    WRITER_TARGET_OS_MACOS,
  };

  // Writes Xcode workspace and project files.
  //
  // |workspace_name| is the optional name of the workspace file name ("all"
  // is used if not specified). |root_target_name| is the name of the main
  // target corresponding to building "All" (for example "gn_all" in Chromium).
  // |ninja_extra_args| are additional arguments to pass to ninja invocation
  // (can be used to increase limit of concurrent processes when using goma).
  // |dir_filters_string| is optional semicolon-separated list of label patterns
  // used to limit the set of generated projects. Only matching targets will be
  // included to the workspace. On failure will populate |err| and return false.
  static bool RunAndWriteFiles(const std::string& workspace_name,
                               const std::string& root_target_name,
                               const std::string& ninja_extra_args,
                               const std::string& dir_filters_string,
                               const BuildSettings* build_settings,
                               const Builder& builder,
                               Err* err);

 private:
  XcodeWriter(const std::string& name);
  ~XcodeWriter();

  // Filters the list of targets to only return the targets with artifacts
  // usable from Xcode (mostly application bundles). On failure populate |err|
  // and return false.
  static bool FilterTargets(const BuildSettings* build_settings,
                            const std::vector<const Target*>& all_targets,
                            const std::string& dir_filters_string,
                            std::vector<const Target*>* targets,
                            Err* err);

  // Generate the "products.xcodeproj" project that reference all products
  // (i.e. targets that have a build artefact usable from Xcode, mostly
  // application bundles).
  void CreateProductsProject(const std::vector<const Target*>& targets,
                             const std::vector<const Target*>& all_targets,
                             const PBXAttributes& attributes,
                             const std::string& source_path,
                             const std::string& config_name,
                             const std::string& root_target,
                             const std::string& ninja_extra_args,
                             const BuildSettings* build_settings,
                             TargetOsType target_os);

  bool WriteFiles(const BuildSettings* build_settings, Err* err);
  bool WriteProjectFile(const BuildSettings* build_settings,
                        PBXProject* project,
                        Err* err);

  void WriteWorkspaceContent(std::ostream& out);
  void WriteProjectContent(std::ostream& out, PBXProject* project);

  std::string name_;
  std::vector<std::unique_ptr<PBXProject>> projects_;

  DISALLOW_COPY_AND_ASSIGN(XcodeWriter);
};

#endif  // TOOLS_GN_XCODE_WRITER_H_
