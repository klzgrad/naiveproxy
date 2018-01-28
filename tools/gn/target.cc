// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/target.h"

#include <stddef.h>

#include "base/bind.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "tools/gn/config_values_extractors.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/functions.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/source_file_type.h"
#include "tools/gn/substitution_writer.h"
#include "tools/gn/tool.h"
#include "tools/gn/toolchain.h"
#include "tools/gn/trace.h"

namespace {

typedef std::set<const Config*> ConfigSet;

// Merges the public configs from the given target to the given config list.
void MergePublicConfigsFrom(const Target* from_target,
                            UniqueVector<LabelConfigPair>* dest) {
  const UniqueVector<LabelConfigPair>& pub = from_target->public_configs();
  dest->Append(pub.begin(), pub.end());
}

// Like MergePublicConfigsFrom above except does the "all dependent" ones. This
// additionally adds all configs to the all_dependent_configs_ of the dest
// target given in *all_dest.
void MergeAllDependentConfigsFrom(const Target* from_target,
                                  UniqueVector<LabelConfigPair>* dest,
                                  UniqueVector<LabelConfigPair>* all_dest) {
  for (const auto& pair : from_target->all_dependent_configs()) {
    all_dest->push_back(pair);
    dest->push_back(pair);
  }
}

Err MakeTestOnlyError(const Target* from, const Target* to) {
  return Err(from->defined_from(), "Test-only dependency not allowed.",
      from->label().GetUserVisibleName(false) + "\n"
      "which is NOT marked testonly can't depend on\n" +
      to->label().GetUserVisibleName(false) + "\n"
      "which is marked testonly. Only targets with \"testonly = true\"\n"
      "can depend on other test-only targets.\n"
      "\n"
      "Either mark it test-only or don't do this dependency.");
}

// Set check_private_deps to true for the first invocation since a target
// can see all of its dependencies. For recursive invocations this will be set
// to false to follow only public dependency paths.
//
// Pass a pointer to an empty set for the first invocation. This will be used
// to avoid duplicate checking.
//
// Checking of object files is optional because it is much slower. This allows
// us to check targets for normal outputs, and then as a second pass check
// object files (since we know it will be an error otherwise). This allows
// us to avoid computing all object file names in the common case.
bool EnsureFileIsGeneratedByDependency(const Target* target,
                                       const OutputFile& file,
                                       bool check_private_deps,
                                       bool consider_object_files,
                                       bool check_data_deps,
                                       std::set<const Target*>* seen_targets) {
  if (seen_targets->find(target) != seen_targets->end())
    return false;  // Already checked this one and it's not found.
  seen_targets->insert(target);

  // Assume that we have relatively few generated inputs so brute-force
  // searching here is OK. If this becomes a bottleneck, consider storing
  // computed_outputs as a hash set.
  for (const OutputFile& cur : target->computed_outputs()) {
    if (file == cur)
      return true;
  }

  if (file == target->write_runtime_deps_output())
    return true;

  // Check binary target intermediate files if requested.
  if (consider_object_files && target->IsBinary()) {
    std::vector<OutputFile> source_outputs;
    for (const SourceFile& source : target->sources()) {
      Toolchain::ToolType tool_type;
      if (!target->GetOutputFilesForSource(source, &tool_type, &source_outputs))
        continue;
      if (base::ContainsValue(source_outputs, file))
        return true;
    }
  }

  if (check_data_deps) {
    check_data_deps = false;  // Consider only direct data_deps.
    for (const auto& pair : target->data_deps()) {
      if (EnsureFileIsGeneratedByDependency(pair.ptr, file, false,
                                            consider_object_files,
                                            check_data_deps, seen_targets))
        return true;  // Found a path.
    }
  }

  // Check all public dependencies (don't do data ones since those are
  // runtime-only).
  for (const auto& pair : target->public_deps()) {
    if (EnsureFileIsGeneratedByDependency(pair.ptr, file, false,
                                          consider_object_files,
                                          check_data_deps, seen_targets))
      return true;  // Found a path.
  }

  // Only check private deps if requested.
  if (check_private_deps) {
    for (const auto& pair : target->private_deps()) {
      if (EnsureFileIsGeneratedByDependency(pair.ptr, file, false,
                                            consider_object_files,
                                            check_data_deps, seen_targets))
        return true;  // Found a path.
    }
    if (target->output_type() == Target::CREATE_BUNDLE) {
      for (auto* dep : target->bundle_data().bundle_deps()) {
        if (EnsureFileIsGeneratedByDependency(dep, file, false,
                                              consider_object_files,
                                              check_data_deps, seen_targets))
          return true;  // Found a path.
      }
    }
  }
  return false;
}

// check_this indicates if the given target should be matched against the
// patterns. It should be set to false for the first call since assert_no_deps
// shouldn't match the target itself.
//
// visited should point to an empty set, this will be used to prevent
// multiple visits.
//
// *failure_path_str will be filled with a string describing the path of the
// dependency failure, and failure_pattern will indicate the pattern in
// assert_no that matched the target.
//
// Returns true if everything is OK. failure_path_str and failure_pattern_index
// will be unchanged in this case.
bool RecursiveCheckAssertNoDeps(const Target* target,
                                bool check_this,
                                const std::vector<LabelPattern>& assert_no,
                                std::set<const Target*>* visited,
                                std::string* failure_path_str,
                                const LabelPattern** failure_pattern) {
  static const char kIndentPath[] = "  ";

  if (visited->find(target) != visited->end())
    return true;  // Already checked this target.
  visited->insert(target);

  if (check_this) {
    // Check this target against the given list of patterns.
    for (const LabelPattern& pattern : assert_no) {
      if (pattern.Matches(target->label())) {
        // Found a match.
        *failure_pattern = &pattern;
        *failure_path_str =
            kIndentPath + target->label().GetUserVisibleName(false);
        return false;
      }
    }
  }

  // Recursively check dependencies.
  for (const auto& pair : target->GetDeps(Target::DEPS_ALL)) {
    if (pair.ptr->output_type() == Target::EXECUTABLE)
      continue;
    if (!RecursiveCheckAssertNoDeps(pair.ptr, true, assert_no, visited,
                                    failure_path_str, failure_pattern)) {
      // To reconstruct the path, prepend the current target to the error.
      std::string prepend_path =
          kIndentPath + target->label().GetUserVisibleName(false) + " ->\n";
      failure_path_str->insert(0, prepend_path);
      return false;
    }
  }

  return true;
}

}  // namespace

const char kExecution_Help[] =
    R"(Build graph and execution overview

Overall build flow

  1. Look for ".gn" file (see "gn help dotfile") in the current directory and
     walk up the directory tree until one is found. Set this directory to be
     the "source root" and interpret this file to find the name of the build
     config file.

  2. Execute the build config file identified by .gn to set up the global
     variables and default toolchain name. Any arguments, variables, defaults,
     etc. set up in this file will be visible to all files in the build.

  3. Load the //BUILD.gn (in the source root directory).

  4. Recursively evaluate rules and load BUILD.gn in other directories as
     necessary to resolve dependencies. If a BUILD file isn't found in the
     specified location, GN will look in the corresponding location inside
     the secondary_source defined in the dotfile (see "gn help dotfile").

  5. When a target's dependencies are resolved, write out the `.ninja`
     file to disk.

  6. When all targets are resolved, write out the root build.ninja file.

Executing target definitions and templates

  Build files are loaded in parallel. This means it is impossible to
  interrogate a target from GN code for any information not derivable from its
  label (see "gn help label"). The exception is the get_target_outputs()
  function which requires the target being interrogated to have been defined
  previously in the same file.

  Targets are declared by their type and given a name:

    static_library("my_static_library") {
      ... target parameter definitions ...
    }

  There is also a generic "target" function for programatically defined types
  (see "gn help target"). You can define new types using templates (see "gn
  help template"). A template defines some custom code that expands to one or
  more other targets.

  Before executing the code inside the target's { }, the target defaults are
  applied (see "gn help set_defaults"). It will inject implicit variable
  definitions that can be overridden by the target code as necessary. Typically
  this mechanism is used to inject a default set of configs that define the
  global compiler and linker flags.

Which targets are built

  All targets encountered in the default toolchain (see "gn help toolchain")
  will have build rules generated for them, even if no other targets reference
  them. Their dependencies must resolve and they will be added to the implicit
  "all" rule (see "gn help ninja_rules").

  Targets in non-default toolchains will only be generated when they are
  required (directly or transitively) to build a target in the default
  toolchain.

  See also "gn help ninja_rules".

Dependencies

  The only difference between "public_deps" and "deps" except for pushing
  configs around the build tree and allowing includes for the purposes of "gn
  check".

  A target's "data_deps" are guaranteed to be built whenever the target is
  built, but the ordering is not defined. The meaning of this is dependencies
  required at runtime. Currently data deps will be complete before the target
  is linked, but this is not semantically guaranteed and this is undesirable
  from a build performance perspective. Since we hope to change this in the
  future, do not rely on this behavior.
)";

Target::Target(const Settings* settings, const Label& label)
    : Item(settings, label),
      output_type_(UNKNOWN),
      output_prefix_override_(false),
      output_extension_set_(false),
      all_headers_public_(true),
      check_includes_(true),
      complete_static_lib_(false),
      testonly_(false),
      toolchain_(nullptr) {}

Target::~Target() {
}

// static
const char* Target::GetStringForOutputType(OutputType type) {
  switch (type) {
    case UNKNOWN:
      return "unknown";
    case GROUP:
      return functions::kGroup;
    case EXECUTABLE:
      return functions::kExecutable;
    case LOADABLE_MODULE:
      return functions::kLoadableModule;
    case SHARED_LIBRARY:
      return functions::kSharedLibrary;
    case STATIC_LIBRARY:
      return functions::kStaticLibrary;
    case SOURCE_SET:
      return functions::kSourceSet;
    case COPY_FILES:
      return functions::kCopy;
    case ACTION:
      return functions::kAction;
    case ACTION_FOREACH:
      return functions::kActionForEach;
    case BUNDLE_DATA:
      return functions::kBundleData;
    case CREATE_BUNDLE:
      return functions::kCreateBundle;
    default:
      return "";
  }
}

Target* Target::AsTarget() {
  return this;
}

const Target* Target::AsTarget() const {
  return this;
}

bool Target::OnResolved(Err* err) {
  DCHECK(output_type_ != UNKNOWN);
  DCHECK(toolchain_) << "Toolchain should have been set before resolving.";

  ScopedTrace trace(TraceItem::TRACE_ON_RESOLVED, label());
  trace.SetToolchain(settings()->toolchain_label());

  // Copy this target's own dependent and public configs to the list of configs
  // applying to it.
  configs_.Append(all_dependent_configs_.begin(), all_dependent_configs_.end());
  MergePublicConfigsFrom(this, &configs_);

  // Copy public configs from all dependencies into the list of configs
  // applying to this target (configs_).
  PullDependentTargetConfigs();

  // Copies public dependencies' public configs to this target's public
  // configs. These configs have already been applied to this target by
  // PullDependentTargetConfigs above, along with the public configs from
  // private deps. This step re-exports them as public configs for targets that
  // depend on this one.
  for (const auto& dep : public_deps_) {
    if (dep.ptr->toolchain() == toolchain()) {
      public_configs_.Append(dep.ptr->public_configs().begin(),
                             dep.ptr->public_configs().end());
    }
  }

  // Copy our own libs and lib_dirs to the final set. This will be from our
  // target and all of our configs. We do this specially since these must be
  // inherited through the dependency tree (other flags don't work this way).
  //
  // This needs to happen after we pull dependent target configs for the
  // public config's libs to be included here. And it needs to happen
  // before pulling the dependent target libs so the libs are in the correct
  // order (local ones first, then the dependency's).
  for (ConfigValuesIterator iter(this); !iter.done(); iter.Next()) {
    const ConfigValues& cur = iter.cur();
    all_lib_dirs_.append(cur.lib_dirs().begin(), cur.lib_dirs().end());
    all_libs_.append(cur.libs().begin(), cur.libs().end());
  }

  PullRecursiveBundleData();
  PullDependentTargetLibs();
  PullRecursiveHardDeps();
  if (!ResolvePrecompiledHeaders(err))
    return false;

  FillOutputFiles();

  if (!CheckVisibility(err))
    return false;
  if (!CheckTestonly(err))
    return false;
  if (!CheckAssertNoDeps(err))
    return false;
  CheckSourcesGenerated();

  if (!write_runtime_deps_output_.value().empty())
    g_scheduler->AddWriteRuntimeDepsTarget(this);

  return true;
}

bool Target::IsBinary() const {
  return output_type_ == EXECUTABLE ||
         output_type_ == SHARED_LIBRARY ||
         output_type_ == LOADABLE_MODULE ||
         output_type_ == STATIC_LIBRARY ||
         output_type_ == SOURCE_SET;
}

bool Target::IsLinkable() const {
  return output_type_ == STATIC_LIBRARY || output_type_ == SHARED_LIBRARY;
}

bool Target::IsFinal() const {
  return output_type_ == EXECUTABLE ||
         output_type_ == SHARED_LIBRARY ||
         output_type_ == LOADABLE_MODULE ||
         output_type_ == ACTION ||
         output_type_ == ACTION_FOREACH ||
         output_type_ == COPY_FILES ||
         output_type_ == CREATE_BUNDLE ||
         (output_type_ == STATIC_LIBRARY && complete_static_lib_);
}

DepsIteratorRange Target::GetDeps(DepsIterationType type) const {
  if (type == DEPS_LINKED) {
    return DepsIteratorRange(DepsIterator(
        &public_deps_, &private_deps_, nullptr));
  }
  // All deps.
  return DepsIteratorRange(DepsIterator(
      &public_deps_, &private_deps_, &data_deps_));
}

std::string Target::GetComputedOutputName() const {
  DCHECK(toolchain_)
      << "Toolchain must be specified before getting the computed output name.";

  const std::string& name = output_name_.empty() ? label().name()
                                                 : output_name_;

  std::string result;
  const Tool* tool = toolchain_->GetToolForTargetFinalOutput(this);
  if (tool) {
    // Only add the prefix if the name doesn't already have it and it's not
    // being overridden.
    if (!output_prefix_override_ &&
        !base::StartsWith(name, tool->output_prefix(),
                          base::CompareCase::SENSITIVE))
      result = tool->output_prefix();
  }
  result.append(name);
  return result;
}

bool Target::SetToolchain(const Toolchain* toolchain, Err* err) {
  DCHECK(!toolchain_);
  DCHECK_NE(UNKNOWN, output_type_);
  toolchain_ = toolchain;

  const Tool* tool = toolchain->GetToolForTargetFinalOutput(this);
  if (tool)
    return true;

  // Tool not specified for this target type.
  if (err) {
    *err = Err(defined_from(), "This target uses an undefined tool.",
        base::StringPrintf(
            "The target %s\n"
            "of type \"%s\"\n"
            "uses toolchain %s\n"
            "which doesn't have the tool \"%s\" defined.\n\n"
            "Alas, I can not continue.",
            label().GetUserVisibleName(false).c_str(),
            GetStringForOutputType(output_type_),
            label().GetToolchainLabel().GetUserVisibleName(false).c_str(),
            Toolchain::ToolTypeToName(
                toolchain->GetToolTypeForTargetFinalOutput(this)).c_str()));
  }
  return false;
}

bool Target::GetOutputFilesForSource(const SourceFile& source,
                                     Toolchain::ToolType* computed_tool_type,
                                     std::vector<OutputFile>* outputs) const {
  outputs->clear();
  *computed_tool_type = Toolchain::TYPE_NONE;

  SourceFileType file_type = GetSourceFileType(source);
  if (file_type == SOURCE_UNKNOWN)
    return false;
  if (file_type == SOURCE_O) {
    // Object files just get passed to the output and not compiled.
    outputs->push_back(OutputFile(settings()->build_settings(), source));
    return true;
  }

  *computed_tool_type = toolchain_->GetToolTypeForSourceType(file_type);
  if (*computed_tool_type == Toolchain::TYPE_NONE)
    return false;  // No tool for this file (it's a header file or something).
  const Tool* tool = toolchain_->GetTool(*computed_tool_type);
  if (!tool)
    return false;  // Tool does not apply for this toolchain.file.

  // Figure out what output(s) this compiler produces.
  SubstitutionWriter::ApplyListToCompilerAsOutputFile(
      this, source, tool->outputs(), outputs);
  return !outputs->empty();
}

void Target::PullDependentTargetConfigs() {
  for (const auto& pair : GetDeps(DEPS_LINKED)) {
    if (pair.ptr->toolchain() == toolchain()) {
      MergeAllDependentConfigsFrom(pair.ptr, &configs_,
                                   &all_dependent_configs_);
    }
  }
  for (const auto& pair : GetDeps(DEPS_LINKED)) {
    if (pair.ptr->toolchain() == toolchain()) {
      MergePublicConfigsFrom(pair.ptr, &configs_);
    }
  }
}

void Target::PullDependentTargetLibsFrom(const Target* dep, bool is_public) {
  // Direct dependent libraries.
  if (dep->output_type() == STATIC_LIBRARY ||
      dep->output_type() == SHARED_LIBRARY ||
      dep->output_type() == SOURCE_SET)
    inherited_libraries_.Append(dep, is_public);

  if (dep->output_type() == SHARED_LIBRARY) {
    // Shared library dependendencies are inherited across public shared
    // library boundaries.
    //
    // In this case:
    //   EXE -> INTERMEDIATE_SHLIB --[public]--> FINAL_SHLIB
    // The EXE will also link to to FINAL_SHLIB. The public dependeny means
    // that the EXE can use the headers in FINAL_SHLIB so the FINAL_SHLIB
    // will need to appear on EXE's link line.
    //
    // However, if the dependency is private:
    //   EXE -> INTERMEDIATE_SHLIB --[private]--> FINAL_SHLIB
    // the dependency will not be propagated because INTERMEDIATE_SHLIB is
    // not granting permission to call functiosn from FINAL_SHLIB. If EXE
    // wants to use functions (and link to) FINAL_SHLIB, it will need to do
    // so explicitly.
    //
    // Static libraries and source sets aren't inherited across shared
    // library boundaries because they will be linked into the shared
    // library.
    inherited_libraries_.AppendPublicSharedLibraries(
        dep->inherited_libraries(), is_public);
  } else if (!dep->IsFinal()) {
    // The current target isn't linked, so propogate linked deps and
    // libraries up the dependency tree.
    inherited_libraries_.AppendInherited(dep->inherited_libraries(), is_public);
  } else if (dep->complete_static_lib()) {
    // Inherit only final targets through _complete_ static libraries.
    //
    // Inherited final libraries aren't linked into complete static libraries.
    // They are forwarded here so that targets that depend on complete
    // static libraries can link them in. Conversely, since complete static
    // libraries link in non-final targets they shouldn't be inherited.
    for (const auto& inherited :
         dep->inherited_libraries().GetOrderedAndPublicFlag()) {
      if (inherited.first->IsFinal()) {
        inherited_libraries_.Append(inherited.first,
                                    is_public && inherited.second);
      }
    }
  }

  // Library settings are always inherited across static library boundaries.
  if (!dep->IsFinal() || dep->output_type() == STATIC_LIBRARY) {
    all_lib_dirs_.append(dep->all_lib_dirs());
    all_libs_.append(dep->all_libs());
  }
}

void Target::PullDependentTargetLibs() {
  for (const auto& dep : public_deps_)
    PullDependentTargetLibsFrom(dep.ptr, true);
  for (const auto& dep : private_deps_)
    PullDependentTargetLibsFrom(dep.ptr, false);
}

void Target::PullRecursiveHardDeps() {
  for (const auto& pair : GetDeps(DEPS_LINKED)) {
    // Direct hard dependencies.
    if (pair.ptr->hard_dep())
      recursive_hard_deps_.insert(pair.ptr);

    // Recursive hard dependencies of all dependencies.
    recursive_hard_deps_.insert(pair.ptr->recursive_hard_deps().begin(),
                                pair.ptr->recursive_hard_deps().end());
  }
}

void Target::PullRecursiveBundleData() {
  for (const auto& pair : GetDeps(DEPS_LINKED)) {
    // Don't propagate bundle_data once they are added to a bundle.
    if (pair.ptr->output_type() == CREATE_BUNDLE)
      continue;

    // Don't propagate across toolchain.
    if (pair.ptr->toolchain() != toolchain())
      continue;

    // Direct dependency on a bundle_data target.
    if (pair.ptr->output_type() == BUNDLE_DATA)
      bundle_data_.AddBundleData(pair.ptr);

    // Recursive bundle_data informations from all dependencies.
    for (auto* target : pair.ptr->bundle_data().bundle_deps())
      bundle_data_.AddBundleData(target);
  }

  bundle_data_.OnTargetResolved(this);
}

void Target::FillOutputFiles() {
  const Tool* tool = toolchain_->GetToolForTargetFinalOutput(this);
  bool check_tool_outputs = false;
  switch (output_type_) {
    case GROUP:
    case BUNDLE_DATA:
    case CREATE_BUNDLE:
    case SOURCE_SET:
    case COPY_FILES:
    case ACTION:
    case ACTION_FOREACH: {
      // These don't get linked to and use stamps which should be the first
      // entry in the outputs. These stamps are named
      // "<target_out_dir>/<targetname>.stamp".
      dependency_output_file_ =
          GetBuildDirForTargetAsOutputFile(this, BuildDirType::OBJ);
      dependency_output_file_.value().append(GetComputedOutputName());
      dependency_output_file_.value().append(".stamp");
      break;
    }
    case EXECUTABLE:
    case LOADABLE_MODULE:
      // Executables and loadable modules don't get linked to, but the first
      // output is used for dependency management.
      CHECK_GE(tool->outputs().list().size(), 1u);
      check_tool_outputs = true;
      dependency_output_file_ =
          SubstitutionWriter::ApplyPatternToLinkerAsOutputFile(
              this, tool, tool->outputs().list()[0]);

      if (tool->runtime_outputs().list().empty()) {
        // Default to the first output for the runtime output.
        runtime_outputs_.push_back(dependency_output_file_);
      } else {
        SubstitutionWriter::ApplyListToLinkerAsOutputFile(
            this, tool, tool->runtime_outputs(), &runtime_outputs_);
      }
      break;
    case STATIC_LIBRARY:
      // Static libraries both have dependencies and linking going off of the
      // first output.
      CHECK(tool->outputs().list().size() >= 1);
      check_tool_outputs = true;
      link_output_file_ = dependency_output_file_ =
          SubstitutionWriter::ApplyPatternToLinkerAsOutputFile(
              this, tool, tool->outputs().list()[0]);
      break;
    case SHARED_LIBRARY:
      CHECK(tool->outputs().list().size() >= 1);
      check_tool_outputs = true;
      if (tool->link_output().empty() && tool->depend_output().empty()) {
        // Default behavior, use the first output file for both.
        link_output_file_ = dependency_output_file_ =
            SubstitutionWriter::ApplyPatternToLinkerAsOutputFile(
                this, tool, tool->outputs().list()[0]);
      } else {
        // Use the tool-specified ones.
        if (!tool->link_output().empty()) {
          link_output_file_ =
              SubstitutionWriter::ApplyPatternToLinkerAsOutputFile(
                  this, tool, tool->link_output());
        }
        if (!tool->depend_output().empty()) {
          dependency_output_file_ =
              SubstitutionWriter::ApplyPatternToLinkerAsOutputFile(
                  this, tool, tool->depend_output());
        }
      }
      if (tool->runtime_outputs().list().empty()) {
        // Default to the link output for the runtime output.
        runtime_outputs_.push_back(link_output_file_);
      } else {
        SubstitutionWriter::ApplyListToLinkerAsOutputFile(
            this, tool, tool->runtime_outputs(), &runtime_outputs_);
      }
      break;
    case UNKNOWN:
    default:
      NOTREACHED();
  }

  // Count anything generated from bundle_data dependencies.
  if (output_type_ == CREATE_BUNDLE)
    bundle_data_.GetOutputFiles(settings(), &computed_outputs_);

  // Count all outputs from this tool as something generated by this target.
  if (check_tool_outputs) {
    SubstitutionWriter::ApplyListToLinkerAsOutputFile(
        this, tool, tool->outputs(), &computed_outputs_);

    // Output names aren't canonicalized in the same way that source files
    // are. For example, the tool outputs often use
    // {{some_var}}/{{output_name}} which expands to "./foo", but this won't
    // match "foo" which is what we'll compute when converting a SourceFile to
    // an OutputFile.
    for (auto& out : computed_outputs_)
      NormalizePath(&out.value());
  }

  // Also count anything the target has declared to be an output.
  std::vector<SourceFile> outputs_as_sources;
  action_values_.GetOutputsAsSourceFiles(this, &outputs_as_sources);
  for (const SourceFile& out : outputs_as_sources)
    computed_outputs_.push_back(OutputFile(settings()->build_settings(), out));
}

bool Target::ResolvePrecompiledHeaders(Err* err) {
  // Precompiled headers are stored on a ConfigValues struct. This way, the
  // build can set all the precompiled header settings in a config and apply
  // it to many targets. Likewise, the precompiled header values may be
  // specified directly on a target.
  //
  // Unlike other values on configs which are lists that just get concatenated,
  // the precompiled header settings are unique values. We allow them to be
  // specified anywhere, but if they are specified in more than one place all
  // places must match.

  // Track where the current settings came from for issuing errors.
  const Label* pch_header_settings_from = NULL;
  if (config_values_.has_precompiled_headers())
    pch_header_settings_from = &label();

  for (ConfigValuesIterator iter(this); !iter.done(); iter.Next()) {
    if (!iter.GetCurrentConfig())
      continue;  // Skip the one on the target itself.

    const Config* config = iter.GetCurrentConfig();
    const ConfigValues& cur = config->resolved_values();
    if (!cur.has_precompiled_headers())
      continue;  // This one has no precompiled header info, skip.

    if (config_values_.has_precompiled_headers()) {
      // Already have a precompiled header values, the settings must match.
      if (config_values_.precompiled_header() != cur.precompiled_header() ||
          config_values_.precompiled_source() != cur.precompiled_source()) {
        *err = Err(defined_from(),
            "Precompiled header setting conflict.",
            "The target " + label().GetUserVisibleName(false) + "\n"
            "has conflicting precompiled header settings.\n"
            "\n"
            "From " + pch_header_settings_from->GetUserVisibleName(false) +
            "\n  header: " + config_values_.precompiled_header() +
            "\n  source: " + config_values_.precompiled_source().value() +
            "\n\n"
            "From " + config->label().GetUserVisibleName(false) +
            "\n  header: " + cur.precompiled_header() +
            "\n  source: " + cur.precompiled_source().value());
        return false;
      }
    } else {
      // Have settings from a config, apply them to ourselves.
      pch_header_settings_from = &config->label();
      config_values_.set_precompiled_header(cur.precompiled_header());
      config_values_.set_precompiled_source(cur.precompiled_source());
    }
  }

  return true;
}

bool Target::CheckVisibility(Err* err) const {
  for (const auto& pair : GetDeps(DEPS_ALL)) {
    if (!Visibility::CheckItemVisibility(this, pair.ptr, err))
      return false;
  }
  return true;
}

bool Target::CheckTestonly(Err* err) const {
  // If the current target is marked testonly, it can include both testonly
  // and non-testonly targets, so there's nothing to check.
  if (testonly())
    return true;

  // Verify no deps have "testonly" set.
  for (const auto& pair : GetDeps(DEPS_ALL)) {
    if (pair.ptr->testonly()) {
      *err = MakeTestOnlyError(this, pair.ptr);
      return false;
    }
  }

  return true;
}

bool Target::CheckAssertNoDeps(Err* err) const {
  if (assert_no_deps_.empty())
    return true;

  std::set<const Target*> visited;
  std::string failure_path_str;
  const LabelPattern* failure_pattern = nullptr;

  if (!RecursiveCheckAssertNoDeps(this, false, assert_no_deps_, &visited,
                                  &failure_path_str, &failure_pattern)) {
    *err = Err(defined_from(), "assert_no_deps failed.",
        label().GetUserVisibleName(false) +
        " has an assert_no_deps entry:\n  " +
        failure_pattern->Describe() +
        "\nwhich fails for the dependency path:\n" +
        failure_path_str);
    return false;
  }
  return true;
}

void Target::CheckSourcesGenerated() const {
  // Checks that any inputs or sources to this target that are in the build
  // directory are generated by a target that this one transitively depends on
  // in some way. We already guarantee that all generated files are written
  // to the build dir.
  //
  // See Scheduler::AddUnknownGeneratedInput's declaration for more.
  for (const SourceFile& file : sources_)
    CheckSourceGenerated(file);
  for (const SourceFile& file : inputs_)
    CheckSourceGenerated(file);
  // TODO(agrieve): Check all_libs_ here as well (those that are source files).
  // http://crbug.com/571731
}

void Target::CheckSourceGenerated(const SourceFile& source) const {
  if (!IsStringInOutputDir(settings()->build_settings()->build_dir(),
                           source.value()))
    return;  // Not in output dir, this is OK.

  // Tell the scheduler about unknown files. This will be noted for later so
  // the list of files written by the GN build itself (often response files)
  // can be filtered out of this list.
  OutputFile out_file(settings()->build_settings(), source);
  std::set<const Target*> seen_targets;
  bool check_data_deps = false;
  bool consider_object_files = false;
  if (!EnsureFileIsGeneratedByDependency(this, out_file, true,
                                         consider_object_files, check_data_deps,
                                         &seen_targets)) {
    seen_targets.clear();
    // Allow dependency to be through data_deps for files generated by gn.
    check_data_deps = g_scheduler->IsFileGeneratedByWriteRuntimeDeps(out_file);
    // Check object files (much slower and very rare) only if the "normal"
    // output check failed.
    consider_object_files = !check_data_deps;
    if (!EnsureFileIsGeneratedByDependency(this, out_file, true,
                                           consider_object_files,
                                           check_data_deps, &seen_targets))
      g_scheduler->AddUnknownGeneratedInput(this, source);
  }
}
