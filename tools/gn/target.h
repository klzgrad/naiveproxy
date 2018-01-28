// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_TARGET_H_
#define TOOLS_GN_TARGET_H_

#include <set>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/logging.h"
#include "base/macros.h"
#include "tools/gn/action_values.h"
#include "tools/gn/bundle_data.h"
#include "tools/gn/config_values.h"
#include "tools/gn/inherited_libraries.h"
#include "tools/gn/item.h"
#include "tools/gn/label_pattern.h"
#include "tools/gn/label_ptr.h"
#include "tools/gn/lib_file.h"
#include "tools/gn/ordered_set.h"
#include "tools/gn/output_file.h"
#include "tools/gn/source_file.h"
#include "tools/gn/toolchain.h"
#include "tools/gn/unique_vector.h"

class DepsIteratorRange;
class Settings;
class Toolchain;

class Target : public Item {
 public:
  enum OutputType {
    UNKNOWN,
    GROUP,
    EXECUTABLE,
    SHARED_LIBRARY,
    LOADABLE_MODULE,
    STATIC_LIBRARY,
    SOURCE_SET,
    COPY_FILES,
    ACTION,
    ACTION_FOREACH,
    BUNDLE_DATA,
    CREATE_BUNDLE,
  };

  enum DepsIterationType {
    DEPS_ALL,  // Iterates through all public, private, and data deps.
    DEPS_LINKED,  // Iterates through all non-data dependencies.
  };

  typedef std::vector<SourceFile> FileList;
  typedef std::vector<std::string> StringVector;

  Target(const Settings* settings, const Label& label);
  ~Target() override;

  // Returns a string naming the output type.
  static const char* GetStringForOutputType(OutputType type);

  // Item overrides.
  Target* AsTarget() override;
  const Target* AsTarget() const override;
  bool OnResolved(Err* err) override;

  OutputType output_type() const { return output_type_; }
  void set_output_type(OutputType t) { output_type_ = t; }

  // True for targets that compile source code (all types of libaries and
  // executables).
  bool IsBinary() const;

  // Can be linked into other targets.
  bool IsLinkable() const;

  // True if the target links dependencies rather than propogated up the graph.
  // This is also true of action and copy steps even though they don't link
  // dependencies, because they also don't propogate libraries up.
  bool IsFinal() const;

  // Will be the empty string to use the target label as the output name.
  // See GetComputedOutputName().
  const std::string& output_name() const { return output_name_; }
  void set_output_name(const std::string& name) { output_name_ = name; }

  // Returns the output name for this target, which is the output_name if
  // specified, or the target label if not.
  //
  // Because this depends on the tool for this target, the toolchain must
  // have been set before calling.
  std::string GetComputedOutputName() const;

  bool output_prefix_override() const { return output_prefix_override_; }
  void set_output_prefix_override(bool prefix_override) {
    output_prefix_override_ = prefix_override;
  }

  // Desired output directory for the final output. This will be used for
  // the {{output_dir}} substitution in the tool if it is specified. If
  // is_null, the tool default will be used.
  const SourceDir& output_dir() const { return output_dir_; }
  void set_output_dir(const SourceDir& dir) { output_dir_ = dir; }

  // The output extension is really a tri-state: unset (output_extension_set
  // is false and the string is empty, meaning the default extension should be
  // used), the output extension is set but empty (output should have no
  // extension) and the output extension is set but nonempty (use the given
  // extension).
  const std::string& output_extension() const { return output_extension_; }
  void set_output_extension(const std::string& extension) {
    output_extension_ = extension;
    output_extension_set_ = true;
  }
  bool output_extension_set() const {
    return output_extension_set_;
  }

  const FileList& sources() const { return sources_; }
  FileList& sources() { return sources_; }

  // Set to true when all sources are public. This is the default. In this case
  // the public headers list should be empty.
  bool all_headers_public() const { return all_headers_public_; }
  void set_all_headers_public(bool p) { all_headers_public_ = p; }

  // When all_headers_public is false, this is the list of public headers. It
  // could be empty which would mean no headers are public.
  const FileList& public_headers() const { return public_headers_; }
  FileList& public_headers() { return public_headers_; }

  // Whether this target's includes should be checked by "gn check".
  bool check_includes() const { return check_includes_; }
  void set_check_includes(bool ci) { check_includes_ = ci; }

  // Whether this static_library target should have code linked in.
  bool complete_static_lib() const { return complete_static_lib_; }
  void set_complete_static_lib(bool complete) {
    DCHECK_EQ(STATIC_LIBRARY, output_type_);
    complete_static_lib_ = complete;
  }

  bool testonly() const { return testonly_; }
  void set_testonly(bool value) { testonly_ = value; }

  OutputFile write_runtime_deps_output() const {
    return write_runtime_deps_output_;
  }
  void set_write_runtime_deps_output(const OutputFile& value) {
    write_runtime_deps_output_ = value;
  }

  // Compile-time extra dependencies.
  const FileList& inputs() const { return inputs_; }
  FileList& inputs() { return inputs_; }

  // Runtime dependencies. These are "file-like things" that can either be
  // directories or files. They do not need to exist, these are just passed as
  // runtime dependencies to external test systems as necessary.
  const std::vector<std::string>& data() const { return data_; }
  std::vector<std::string>& data() { return data_; }

  // Information about the bundle. Only valid for CREATE_BUNDLE target after
  // they have been resolved.
  const BundleData& bundle_data() const { return bundle_data_; }
  BundleData& bundle_data() { return bundle_data_; }

  // Returns true if targets depending on this one should have an order
  // dependency.
  bool hard_dep() const {
    return output_type_ == ACTION ||
           output_type_ == ACTION_FOREACH ||
           output_type_ == COPY_FILES ||
           output_type_ == CREATE_BUNDLE;
  }

  // Returns the iterator range which can be used in range-based for loops
  // to iterate over multiple types of deps in one loop:
  //   for (const auto& pair : target->GetDeps(Target::DEPS_ALL)) ...
  DepsIteratorRange GetDeps(DepsIterationType type) const;

  // Linked private dependencies.
  const LabelTargetVector& private_deps() const { return private_deps_; }
  LabelTargetVector& private_deps() { return private_deps_; }

  // Linked public dependencies.
  const LabelTargetVector& public_deps() const { return public_deps_; }
  LabelTargetVector& public_deps() { return public_deps_; }

  // Non-linked dependencies.
  const LabelTargetVector& data_deps() const { return data_deps_; }
  LabelTargetVector& data_deps() { return data_deps_; }

  // List of configs that this class inherits settings from. Once a target is
  // resolved, this will also list all-dependent and public configs.
  const UniqueVector<LabelConfigPair>& configs() const { return configs_; }
  UniqueVector<LabelConfigPair>& configs() { return configs_; }

  // List of configs that all dependencies (direct and indirect) of this
  // target get. These configs are not added to this target. Note that due
  // to the way this is computed, there may be duplicates in this list.
  const UniqueVector<LabelConfigPair>& all_dependent_configs() const {
    return all_dependent_configs_;
  }
  UniqueVector<LabelConfigPair>& all_dependent_configs() {
    return all_dependent_configs_;
  }

  // List of configs that targets depending directly on this one get. These
  // configs are also added to this target.
  const UniqueVector<LabelConfigPair>& public_configs() const {
    return public_configs_;
  }
  UniqueVector<LabelConfigPair>& public_configs() {
    return public_configs_;
  }

  // Dependencies that can include files from this target.
  const std::set<Label>& allow_circular_includes_from() const {
    return allow_circular_includes_from_;
  }
  std::set<Label>& allow_circular_includes_from() {
    return allow_circular_includes_from_;
  }

  const InheritedLibraries& inherited_libraries() const {
    return inherited_libraries_;
  }

  // This config represents the configuration set directly on this target.
  ConfigValues& config_values() { return config_values_; }
  const ConfigValues& config_values() const { return config_values_; }

  ActionValues& action_values() { return action_values_; }
  const ActionValues& action_values() const { return action_values_; }

  const OrderedSet<SourceDir>& all_lib_dirs() const { return all_lib_dirs_; }
  const OrderedSet<LibFile>& all_libs() const { return all_libs_; }

  const std::set<const Target*>& recursive_hard_deps() const {
    return recursive_hard_deps_;
  }

  std::vector<LabelPattern>& assert_no_deps() {
    return assert_no_deps_;
  }
  const std::vector<LabelPattern>& assert_no_deps() const {
    return assert_no_deps_;
  }

  // The toolchain is only known once this target is resolved (all if its
  // dependencies are known). They will be null until then. Generally, this can
  // only be used during target writing.
  const Toolchain* toolchain() const { return toolchain_; }

  // Sets the toolchain. The toolchain must include a tool for this target
  // or the error will be set and the function will return false. Unusually,
  // this function's "err" output is optional since this is commonly used
  // frequently by unit tests which become needlessly verbose.
  bool SetToolchain(const Toolchain* toolchain, Err* err = nullptr);

  // Once this target has been resolved, all outputs from the target will be
  // listed here. This will include things listed in the "outputs" for an
  // action or a copy step, and the output library or executable file(s) from
  // binary targets.
  //
  // It will NOT include stamp files and object files.
  const std::vector<OutputFile>& computed_outputs() const {
    return computed_outputs_;
  }

  // Returns outputs from this target. The link output file is the one that
  // other targets link to when they depend on this target. This will only be
  // valid for libraries and will be empty for all other target types.
  //
  // The dependency output file is the file that should be used to express
  // a dependency on this one. It could be the same as the link output file
  // (this will be the case for static libraries). For shared libraries it
  // could be the same or different than the link output file, depending on the
  // system. For actions this will be the stamp file.
  //
  // These are only known once the target is resolved and will be empty before
  // that. This is a cache of the files to prevent every target that depends on
  // a given library from recomputing the same pattern.
  const OutputFile& link_output_file() const {
    return link_output_file_;
  }
  const OutputFile& dependency_output_file() const {
    return dependency_output_file_;
  }

  // The subset of computed_outputs that are considered runtime outputs.
  const std::vector<OutputFile>& runtime_outputs() const {
    return runtime_outputs_;
  }

  // Computes the set of output files resulting from compiling the given source
  // file. If the file can be compiled and the tool exists, fills the outputs
  // in and writes the tool type to computed_tool_type. If the file is not
  // compilable, returns false.
  //
  // The function can succeed with a "NONE" tool type for object files which
  // are just passed to the output. The output will always be overwritten, not
  // appended to.
  bool GetOutputFilesForSource(const SourceFile& source,
                               Toolchain::ToolType* computed_tool_type,
                               std::vector<OutputFile>* outputs) const;

 private:
  FRIEND_TEST_ALL_PREFIXES(Target, ResolvePrecompiledHeaders);

  // Pulls necessary information from dependencies to this one when all
  // dependencies have been resolved.
  void PullDependentTargetConfigs();
  void PullDependentTargetLibsFrom(const Target* dep, bool is_public);
  void PullDependentTargetLibs();
  void PullRecursiveHardDeps();
  void PullRecursiveBundleData();

  // Fills the link and dependency output files when a target is resolved.
  void FillOutputFiles();

  // Checks precompiled headers from configs and makes sure the resulting
  // values are in config_values_.
  bool ResolvePrecompiledHeaders(Err* err);

  // Validates the given thing when a target is resolved.
  bool CheckVisibility(Err* err) const;
  bool CheckTestonly(Err* err) const;
  bool CheckAssertNoDeps(Err* err) const;
  void CheckSourcesGenerated() const;
  void CheckSourceGenerated(const SourceFile& source) const;

  OutputType output_type_;
  std::string output_name_;
  bool output_prefix_override_;
  SourceDir output_dir_;
  std::string output_extension_;
  bool output_extension_set_;

  FileList sources_;
  bool all_headers_public_;
  FileList public_headers_;
  bool check_includes_;
  bool complete_static_lib_;
  bool testonly_;
  FileList inputs_;
  std::vector<std::string> data_;
  BundleData bundle_data_;
  OutputFile write_runtime_deps_output_;

  LabelTargetVector private_deps_;
  LabelTargetVector public_deps_;
  LabelTargetVector data_deps_;

  // See getters for more info.
  UniqueVector<LabelConfigPair> configs_;
  UniqueVector<LabelConfigPair> all_dependent_configs_;
  UniqueVector<LabelConfigPair> public_configs_;

  std::set<Label> allow_circular_includes_from_;

  // Static libraries, shared libraries, and source sets from transitive deps
  // that need to be linked.
  InheritedLibraries inherited_libraries_;

  // These libs and dirs are inherited from statically linked deps and all
  // configs applying to this target.
  OrderedSet<SourceDir> all_lib_dirs_;
  OrderedSet<LibFile> all_libs_;

  // All hard deps from this target and all dependencies. Filled in when this
  // target is marked resolved. This will not include the current target.
  std::set<const Target*> recursive_hard_deps_;

  std::vector<LabelPattern> assert_no_deps_;

  // Used for all binary targets. The precompiled header values in this struct
  // will be resolved to the ones to use for this target, if precompiled
  // headers are used.
  ConfigValues config_values_;

  // Used for action[_foreach] targets.
  ActionValues action_values_;

  // Toolchain used by this target. Null until target is resolved.
  const Toolchain* toolchain_;

  // Output files. Empty until the target is resolved.
  std::vector<OutputFile> computed_outputs_;
  OutputFile link_output_file_;
  OutputFile dependency_output_file_;
  std::vector<OutputFile> runtime_outputs_;

  DISALLOW_COPY_AND_ASSIGN(Target);
};

extern const char kExecution_Help[];

#endif  // TOOLS_GN_TARGET_H_
