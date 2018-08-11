// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <set>

#include "base/strings/string_number_conversions.h"
#include "tools/gn/commands.h"
#include "tools/gn/config.h"
#include "tools/gn/config_values_extractors.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/desc_builder.h"
#include "tools/gn/input_file.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/runtime_deps.h"
#include "tools/gn/settings.h"
#include "tools/gn/substitution_writer.h"
#include "tools/gn/variables.h"

// Example structure of Value for single target
// (not applicable or empty fields will be ommitted depending on target type)
//
// target_properties = {
//   "type" : "output_type", // matching Target::GetStringForOutputType
//   "toolchain" : "toolchain_name",
//   "visibility" : [ list of visibility pattern descriptions ],
//   "test_only" : true or false,
//   "check_includes": true or false,
//   "allow_circular_includes_from": [ list of target names ],
//   "sources" : [ list of source files ],
//   "public" : either "*" or [ list of public headers],
//   "inputs" : [ list of inputs for target ],
//   "configs" : [ list of configs for this target ],
//   "public_configs" : [ list of public configs for this taget],
//   "all_dependent_configs", [ list of all dependent configs for this target],
//   "script" : "script for action targets",
//   "args" : [ argument list for action targets ],
//   "depfile : "file name for action input dependencies",
//   "outputs" : [ list of target outputs ],
//   "arflags", "asmflags", "cflags", "cflags_c",
//   "clfags_cc", "cflags_objc", "clfags_objcc" : [ list of flags],
//   "defines" : [ list of preprocessor definitions ],
//   "include_dirs" : [ list of include directories ],
//   "precompiled_header" : "name of precompiled header file",
//   "precompiled_source" : "path to precompiled source",
//   "deps : [ list of target dependencies ],
//   "libs" : [ list of libraries ],
//   "lib_dirs" : [ list of library directories ]
// }
//
// Optionally, if "what" is specified while generating description, two other
// properties can be requested that are not included by default
//
// "runtime_deps" : [list of computed runtime dependencies]
// "source_outputs" : {
//    "source_file x" : [ list of outputs for source file x ]
//    "source_file y" : [ list of outputs for source file y ]
//    ...
// }

namespace {

std::string FormatSourceDir(const SourceDir& dir) {
#if defined(OS_WIN)
  // On Windows we fix up system absolute paths to look like native ones.
  // Internally, they'll look like "/C:\foo\bar/"
  if (dir.is_system_absolute()) {
    std::string buf = dir.value();
    if (buf.size() > 3 && buf[2] == ':') {
      buf.erase(buf.begin());  // Erase beginning slash.
      return buf;
    }
  }
#endif
  return dir.value();
}

void RecursiveCollectChildDeps(const Target* target,
                               std::set<const Target*>* result);

void RecursiveCollectDeps(const Target* target,
                          std::set<const Target*>* result) {
  if (result->find(target) != result->end())
    return;  // Already did this target.
  result->insert(target);

  RecursiveCollectChildDeps(target, result);
}

void RecursiveCollectChildDeps(const Target* target,
                               std::set<const Target*>* result) {
  for (const auto& pair : target->GetDeps(Target::DEPS_ALL))
    RecursiveCollectDeps(pair.ptr, result);
}

// Common functionality for target and config description builder
class BaseDescBuilder {
 public:
  typedef std::unique_ptr<base::Value> ValuePtr;

  BaseDescBuilder(const std::set<std::string>& what,
                  bool all,
                  bool tree,
                  bool blame)
      : what_(what), all_(all), tree_(tree), blame_(blame) {}

 protected:
  virtual Label GetToolchainLabel() const = 0;

  bool what(const std::string& w) const {
    return what_.empty() || what_.find(w) != what_.end();
  }

  template <typename T>
  ValuePtr RenderValue(const std::vector<T>& vector) {
    auto res = std::make_unique<base::ListValue>();
    for (const auto& v : vector)
      res->Append(RenderValue(v));

    return std::move(res);
  }

  ValuePtr RenderValue(const std::string& s, bool optional = false) {
    return (s.empty() && optional) ? std::make_unique<base::Value>()
                                   : ValuePtr(new base::Value(s));
  }

  ValuePtr RenderValue(const SourceDir& d) {
    return d.is_null() ? std::make_unique<base::Value>()
                       : ValuePtr(new base::Value(FormatSourceDir(d)));
  }

  ValuePtr RenderValue(const SourceFile& f) {
    return f.is_null() ? std::make_unique<base::Value>()
                       : ValuePtr(new base::Value(f.value()));
  }

  ValuePtr RenderValue(const SourceFile* f) { return RenderValue(*f); }

  ValuePtr RenderValue(const LibFile& lib) {
    if (lib.is_source_file())
      return RenderValue(lib.source_file());
    return RenderValue(lib.value());
  }

  template <class VectorType>
  void FillInConfigVector(base::ListValue* out,
                          const VectorType& configs,
                          int indent = 0) {
    for (const auto& config : configs) {
      std::string name(indent * 2, ' ');
      name.append(config.label.GetUserVisibleName(GetToolchainLabel()));
      out->AppendString(name);
      if (tree_)
        FillInConfigVector(out, config.ptr->configs(), indent + 1);
    }
  }

  void FillInPrecompiledHeader(base::DictionaryValue* out,
                               const ConfigValues& values) {
    if (what(variables::kPrecompiledHeader) &&
        !values.precompiled_header().empty()) {
      out->SetWithoutPathExpansion(
          variables::kPrecompiledHeader,
          RenderValue(values.precompiled_header(), true));
    }
    if (what(variables::kPrecompiledSource) &&
        !values.precompiled_source().is_null()) {
      out->SetWithoutPathExpansion(variables::kPrecompiledSource,
                                   RenderValue(values.precompiled_source()));
    }
  }

  std::set<std::string> what_;
  bool all_;
  bool tree_;
  bool blame_;
};

class ConfigDescBuilder : public BaseDescBuilder {
 public:
  ConfigDescBuilder(const Config* config, const std::set<std::string>& what)
      : BaseDescBuilder(what, false, false, false), config_(config) {}

  std::unique_ptr<base::DictionaryValue> BuildDescription() {
    auto res = std::make_unique<base::DictionaryValue>();
    const ConfigValues& values = config_->resolved_values();

    if (what_.empty())
      res->SetKey(
          "toolchain",
          base::Value(
              config_->label().GetToolchainLabel().GetUserVisibleName(false)));

    if (what(variables::kConfigs) && !config_->configs().empty()) {
      auto configs = std::make_unique<base::ListValue>();
      FillInConfigVector(configs.get(), config_->configs().vector());
      res->SetWithoutPathExpansion(variables::kConfigs, std::move(configs));
    }

#define CONFIG_VALUE_ARRAY_HANDLER(name, type)                        \
  if (what(#name)) {                                                  \
    ValuePtr ptr =                                                    \
        render_config_value_array<type>(values, &ConfigValues::name); \
    if (ptr) {                                                        \
      res->SetWithoutPathExpansion(#name, std::move(ptr));            \
    }                                                                 \
  }
    CONFIG_VALUE_ARRAY_HANDLER(arflags, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(asmflags, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(cflags, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(cflags_c, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(cflags_cc, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(cflags_objc, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(cflags_objcc, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(defines, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(include_dirs, SourceDir)
    CONFIG_VALUE_ARRAY_HANDLER(inputs, SourceFile)
    CONFIG_VALUE_ARRAY_HANDLER(ldflags, std::string)
    CONFIG_VALUE_ARRAY_HANDLER(lib_dirs, SourceDir)
    CONFIG_VALUE_ARRAY_HANDLER(libs, LibFile)

#undef CONFIG_VALUE_ARRAY_HANDLER

    FillInPrecompiledHeader(res.get(), values);

    return res;
  }

 protected:
  Label GetToolchainLabel() const override {
    return config_->label().GetToolchainLabel();
  }

 private:
  template <typename T>
  ValuePtr render_config_value_array(
      const ConfigValues& values,
      const std::vector<T>& (ConfigValues::*getter)() const) {
    auto res = std::make_unique<base::ListValue>();

    for (const T& cur : (values.*getter)())
      res->Append(RenderValue(cur));

    return res->empty() ? nullptr : std::move(res);
  }

  const Config* config_;
};

class TargetDescBuilder : public BaseDescBuilder {
 public:
  TargetDescBuilder(const Target* target,
                    const std::set<std::string>& what,
                    bool all,
                    bool tree,
                    bool blame)
      : BaseDescBuilder(what, all, tree, blame), target_(target) {}

  std::unique_ptr<base::DictionaryValue> BuildDescription() {
    auto res = std::make_unique<base::DictionaryValue>();
    bool is_binary_output = target_->IsBinary();

    if (what_.empty()) {
      res->SetKey(
          "type",
          base::Value(Target::GetStringForOutputType(target_->output_type())));
      res->SetKey(
          "toolchain",
          base::Value(
              target_->label().GetToolchainLabel().GetUserVisibleName(false)));
    }

    // General target meta variables.
    if (what(variables::kVisibility))
      res->SetWithoutPathExpansion(variables::kVisibility,
                                   target_->visibility().AsValue());

    if (what(variables::kTestonly))
      res->SetKey(variables::kTestonly, base::Value(target_->testonly()));

    if (is_binary_output) {
      if (what(variables::kCheckIncludes))
        res->SetKey(variables::kCheckIncludes,
                    base::Value(target_->check_includes()));

      if (what(variables::kAllowCircularIncludesFrom)) {
        auto labels = std::make_unique<base::ListValue>();
        for (const auto& cur : target_->allow_circular_includes_from())
          labels->AppendString(cur.GetUserVisibleName(GetToolchainLabel()));

        res->SetWithoutPathExpansion(variables::kAllowCircularIncludesFrom,
                                     std::move(labels));
      }
    }

    if (what(variables::kSources) && !target_->sources().empty())
      res->SetWithoutPathExpansion(variables::kSources,
                                   RenderValue(target_->sources()));

    if (what(variables::kOutputName) && !target_->output_name().empty())
      res->SetKey(variables::kOutputName, base::Value(target_->output_name()));

    if (what(variables::kOutputDir) && !target_->output_dir().is_null())
      res->SetWithoutPathExpansion(variables::kOutputDir,
                                   RenderValue(target_->output_dir()));

    if (what(variables::kOutputExtension) && target_->output_extension_set())
      res->SetKey(variables::kOutputExtension,
                  base::Value(target_->output_extension()));

    if (what(variables::kPublic)) {
      if (target_->all_headers_public())
        res->SetKey(variables::kPublic, base::Value("*"));
      else
        res->SetWithoutPathExpansion(variables::kPublic,
                                     RenderValue(target_->public_headers()));
    }

    if (what(variables::kInputs)) {
      std::vector<const SourceFile*> inputs;
      for (ConfigValuesIterator iter(target_); !iter.done(); iter.Next()) {
        for (const auto& input : iter.cur().inputs())
          inputs.push_back(&input);
      }
      if (!inputs.empty())
        res->SetWithoutPathExpansion(variables::kInputs, RenderValue(inputs));
    }

    if (is_binary_output && what(variables::kConfigs) &&
        !target_->configs().empty()) {
      auto configs = std::make_unique<base::ListValue>();
      FillInConfigVector(configs.get(), target_->configs().vector());
      res->SetWithoutPathExpansion(variables::kConfigs, std::move(configs));
    }

    if (what(variables::kPublicConfigs) && !target_->public_configs().empty()) {
      auto configs = std::make_unique<base::ListValue>();
      FillInConfigVector(configs.get(), target_->public_configs());
      res->SetWithoutPathExpansion(variables::kPublicConfigs,
                                   std::move(configs));
    }

    if (what(variables::kAllDependentConfigs) &&
        !target_->all_dependent_configs().empty()) {
      auto configs = std::make_unique<base::ListValue>();
      FillInConfigVector(configs.get(), target_->all_dependent_configs());
      res->SetWithoutPathExpansion(variables::kAllDependentConfigs,
                                   std::move(configs));
    }

    // Action
    if (target_->output_type() == Target::ACTION ||
        target_->output_type() == Target::ACTION_FOREACH) {
      if (what(variables::kScript))
        res->SetKey(variables::kScript,
                    base::Value(target_->action_values().script().value()));

      if (what(variables::kArgs)) {
        auto args = std::make_unique<base::ListValue>();
        for (const auto& elem : target_->action_values().args().list())
          args->AppendString(elem.AsString());

        res->SetWithoutPathExpansion(variables::kArgs, std::move(args));
      }
      if (what(variables::kDepfile) &&
          !target_->action_values().depfile().empty()) {
        res->SetKey(variables::kDepfile,
                    base::Value(target_->action_values().depfile().AsString()));
      }
    }

    if (target_->output_type() != Target::SOURCE_SET &&
        target_->output_type() != Target::GROUP &&
        target_->output_type() != Target::BUNDLE_DATA) {
      if (what(variables::kOutputs))
        FillInOutputs(res.get());
    }

    // Source outputs are only included when specifically asked for it
    if (what_.find("source_outputs") != what_.end())
      FillInSourceOutputs(res.get());

    if (target_->output_type() == Target::CREATE_BUNDLE && what("bundle_data"))
      FillInBundle(res.get());

    if (is_binary_output) {
#define CONFIG_VALUE_ARRAY_HANDLER(name, type)                    \
  if (what(#name)) {                                              \
    ValuePtr ptr = RenderConfigValues<type>(&ConfigValues::name); \
    if (ptr) {                                                    \
      res->SetWithoutPathExpansion(#name, std::move(ptr));        \
    }                                                             \
  }
      CONFIG_VALUE_ARRAY_HANDLER(arflags, std::string)
      CONFIG_VALUE_ARRAY_HANDLER(asmflags, std::string)
      CONFIG_VALUE_ARRAY_HANDLER(cflags, std::string)
      CONFIG_VALUE_ARRAY_HANDLER(cflags_c, std::string)
      CONFIG_VALUE_ARRAY_HANDLER(cflags_cc, std::string)
      CONFIG_VALUE_ARRAY_HANDLER(cflags_objc, std::string)
      CONFIG_VALUE_ARRAY_HANDLER(cflags_objcc, std::string)
      CONFIG_VALUE_ARRAY_HANDLER(defines, std::string)
      CONFIG_VALUE_ARRAY_HANDLER(include_dirs, SourceDir)
      CONFIG_VALUE_ARRAY_HANDLER(inputs, SourceFile)
      CONFIG_VALUE_ARRAY_HANDLER(ldflags, std::string)
#undef CONFIG_VALUE_ARRAY_HANDLER

      // Libs and lib_dirs are handled specially below.

      FillInPrecompiledHeader(res.get(), target_->config_values());
    }

    if (what(variables::kDeps))
      res->SetWithoutPathExpansion(variables::kDeps, RenderDeps());

    // Runtime deps are special, print only when explicitly asked for and not in
    // overview mode.
    if (what_.find("runtime_deps") != what_.end())
      res->SetWithoutPathExpansion("runtime_deps", RenderRuntimeDeps());

    // libs and lib_dirs are special in that they're inherited. We don't
    // currently
    // implement a blame feature for this since the bottom-up inheritance makes
    // this difficult.

    // Libs can be part of any target and get recursively pushed up the chain,
    // so display them regardless of target type.
    if (what(variables::kLibs)) {
      const OrderedSet<LibFile>& all_libs = target_->all_libs();
      if (!all_libs.empty()) {
        auto libs = std::make_unique<base::ListValue>();
        for (size_t i = 0; i < all_libs.size(); i++)
          libs->AppendString(all_libs[i].value());
        res->SetWithoutPathExpansion(variables::kLibs, std::move(libs));
      }
    }

    if (what(variables::kLibDirs)) {
      const OrderedSet<SourceDir>& all_lib_dirs = target_->all_lib_dirs();
      if (!all_lib_dirs.empty()) {
        auto lib_dirs = std::make_unique<base::ListValue>();
        for (size_t i = 0; i < all_lib_dirs.size(); i++)
          lib_dirs->AppendString(FormatSourceDir(all_lib_dirs[i]));
        res->SetWithoutPathExpansion(variables::kLibDirs, std::move(lib_dirs));
      }
    }

    return res;
  }

 private:
  // Prints dependencies of the given target (not the target itself). If the
  // set is non-null, new targets encountered will be added to the set, and if
  // a dependency is in the set already, it will not be recused into. When the
  // set is null, all dependencies will be printed.
  void RecursivePrintDeps(base::ListValue* out,
                          const Target* target,
                          std::set<const Target*>* seen_targets,
                          int indent_level) {
    // Combine all deps into one sorted list.
    std::vector<LabelTargetPair> sorted_deps;
    for (const auto& pair : target->GetDeps(Target::DEPS_ALL))
      sorted_deps.push_back(pair);
    std::sort(sorted_deps.begin(), sorted_deps.end(),
              LabelPtrLabelLess<Target>());

    std::string indent(indent_level * 2, ' ');

    for (const auto& pair : sorted_deps) {
      const Target* cur_dep = pair.ptr;
      std::string str =
          indent + cur_dep->label().GetUserVisibleName(GetToolchainLabel());

      bool print_children = true;
      if (seen_targets) {
        if (seen_targets->find(cur_dep) == seen_targets->end()) {
          // New target, mark it visited.
          seen_targets->insert(cur_dep);
        } else {
          // Already seen.
          print_children = false;
          // Only print "..." if something is actually elided, which means that
          // the current target has children.
          if (!cur_dep->public_deps().empty() ||
              !cur_dep->private_deps().empty() || !cur_dep->data_deps().empty())
            str += "...";
        }
      }

      out->AppendString(str);

      if (print_children)
        RecursivePrintDeps(out, cur_dep, seen_targets, indent_level + 1);
    }
  }

  ValuePtr RenderDeps() {
    auto res = std::make_unique<base::ListValue>();

    // Tree mode is separate.
    if (tree_) {
      if (all_) {
        // Show all tree deps with no eliding.
        RecursivePrintDeps(res.get(), target_, nullptr, 0);
      } else {
        // Don't recurse into duplicates.
        std::set<const Target*> seen_targets;
        RecursivePrintDeps(res.get(), target_, &seen_targets, 0);
      }
    } else {  // not tree

      // Collect the deps to display.
      if (all_) {
        // Show all dependencies.
        std::set<const Target*> all_deps;
        RecursiveCollectChildDeps(target_, &all_deps);
        commands::FilterAndPrintTargetSet(all_deps, res.get());
      } else {
        // Show direct dependencies only.
        std::vector<const Target*> deps;
        for (const auto& pair : target_->GetDeps(Target::DEPS_ALL))
          deps.push_back(pair.ptr);
        std::sort(deps.begin(), deps.end());
        commands::FilterAndPrintTargets(&deps, res.get());
      }
    }

    return std::move(res);
  }

  ValuePtr RenderRuntimeDeps() {
    auto res = std::make_unique<base::ListValue>();

    const Target* previous_from = NULL;
    for (const auto& pair : ComputeRuntimeDeps(target_)) {
      std::string str;
      if (blame_) {
        // Generally a target's runtime deps will be listed sequentially, so
        // group them and don't duplicate the "from" label for two in a row.
        if (previous_from == pair.second) {
          str = "  ";
        } else {
          previous_from = pair.second;
          res->AppendString(
              str + "From " +
              pair.second->label().GetUserVisibleName(GetToolchainLabel()));
          str = "  ";
        }
      }

      res->AppendString(str + pair.first.value());
    }

    return std::move(res);
  }

  void FillInSourceOutputs(base::DictionaryValue* res) {
    auto dict = std::make_unique<base::DictionaryValue>();
    for (const auto& source : target_->sources()) {
      std::vector<OutputFile> outputs;
      Toolchain::ToolType tool_type = Toolchain::TYPE_NONE;
      if (target_->GetOutputFilesForSource(source, &tool_type, &outputs)) {
        auto list = std::make_unique<base::ListValue>();
        for (const auto& output : outputs)
          list->AppendString(output.value());

        dict->SetWithoutPathExpansion(source.value(), std::move(list));
      }
    }
    res->SetWithoutPathExpansion("source_outputs", std::move(dict));
  }

  void FillInBundle(base::DictionaryValue* res) {
    auto data = std::make_unique<base::DictionaryValue>();
    const BundleData& bundle_data = target_->bundle_data();
    const Settings* settings = target_->settings();
    BundleData::SourceFiles sources;
    bundle_data.GetSourceFiles(&sources);
    data->SetWithoutPathExpansion("source_files", RenderValue(sources));
    data->SetKey(
        "root_dir_output",
        base::Value(bundle_data.GetBundleRootDirOutput(settings).value()));
    data->SetWithoutPathExpansion("root_dir",
                                  RenderValue(bundle_data.root_dir()));
    data->SetWithoutPathExpansion("resources_dir",
                                  RenderValue(bundle_data.resources_dir()));
    data->SetWithoutPathExpansion("executable_dir",
                                  RenderValue(bundle_data.executable_dir()));
    data->SetWithoutPathExpansion("plugins_dir",
                                  RenderValue(bundle_data.plugins_dir()));
    data->SetKey("product_type", base::Value(bundle_data.product_type()));
    data->SetWithoutPathExpansion(
        "partial_info_plist", RenderValue(bundle_data.partial_info_plist()));

    auto deps = std::make_unique<base::ListValue>();
    for (const auto* dep : bundle_data.bundle_deps())
      deps->AppendString(dep->label().GetUserVisibleName(GetToolchainLabel()));

    data->SetWithoutPathExpansion("deps", std::move(deps));
    res->SetWithoutPathExpansion("bundle_data", std::move(data));
  }

  void FillInOutputs(base::DictionaryValue* res) {
    if (target_->output_type() == Target::ACTION) {
      auto list = std::make_unique<base::ListValue>();
      for (const auto& elem : target_->action_values().outputs().list())
        list->AppendString(elem.AsString());

      res->SetWithoutPathExpansion(variables::kOutputs, std::move(list));
    } else if (target_->output_type() == Target::CREATE_BUNDLE) {
      std::vector<SourceFile> output_files;
      target_->bundle_data().GetOutputsAsSourceFiles(target_->settings(),
                                                     &output_files);
      res->SetWithoutPathExpansion(variables::kOutputs,
                                   RenderValue(output_files));
    } else if (target_->output_type() == Target::ACTION_FOREACH ||
               target_->output_type() == Target::COPY_FILES) {
      const SubstitutionList& outputs = target_->action_values().outputs();
      if (!outputs.required_types().empty()) {
        auto patterns = std::make_unique<base::ListValue>();
        for (const auto& elem : outputs.list())
          patterns->AppendString(elem.AsString());

        res->SetWithoutPathExpansion("output_patterns", std::move(patterns));
      }
      std::vector<SourceFile> output_files;
      SubstitutionWriter::ApplyListToSources(target_, target_->settings(),
                                             outputs, target_->sources(),
                                             &output_files);
      res->SetWithoutPathExpansion(variables::kOutputs,
                                   RenderValue(output_files));
    } else {
      DCHECK(target_->IsBinary());
      const Tool* tool =
          target_->toolchain()->GetToolForTargetFinalOutput(target_);

      std::vector<OutputFile> output_files;
      SubstitutionWriter::ApplyListToLinkerAsOutputFile(
          target_, tool, tool->outputs(), &output_files);
      std::vector<SourceFile> output_files_as_source_file;
      for (const OutputFile& output_file : output_files)
        output_files_as_source_file.push_back(
            output_file.AsSourceFile(target_->settings()->build_settings()));

      res->SetWithoutPathExpansion(variables::kOutputs,
                                   RenderValue(output_files_as_source_file));
    }
  }

  // Writes a given config value type to the string, optionally with
  // attribution.
  // This should match RecursiveTargetConfigToStream in the order it traverses.
  template <class T>
  ValuePtr RenderConfigValues(const std::vector<T>& (ConfigValues::*getter)()
                                  const) {
    auto res = std::make_unique<base::ListValue>();
    for (ConfigValuesIterator iter(target_); !iter.done(); iter.Next()) {
      const std::vector<T>& vec = (iter.cur().*getter)();

      if (vec.empty())
        continue;

      if (blame_) {
        const Config* config = iter.GetCurrentConfig();
        if (config) {
          // Source of this value is a config.
          std::string from =
              "From " + config->label().GetUserVisibleName(false);
          res->AppendString(from);
          if (iter.origin()) {
            Location location = iter.origin()->GetRange().begin();
            from = "     (Added by " + location.file()->name().value() + ":" +
                   base::IntToString(location.line_number()) + ")";
            res->AppendString(from);
          }
        } else {
          // Source of this value is the target itself.
          std::string from =
              "From " + target_->label().GetUserVisibleName(false);
          res->AppendString(from);
        }
      }

      for (const T& val : vec) {
        ValuePtr rendered = RenderValue(val);
        std::string str;
        // Indent string values in blame mode
        if (blame_ && rendered->GetAsString(&str)) {
          str = "  " + str;
          rendered = std::make_unique<base::Value>(str);
        }
        res->Append(std::move(rendered));
      }
    }
    return res->empty() ? nullptr : std::move(res);
  }

  Label GetToolchainLabel() const override {
    return target_->label().GetToolchainLabel();
  }

  const Target* target_;
};

}  // namespace

std::unique_ptr<base::DictionaryValue> DescBuilder::DescriptionForTarget(
    const Target* target,
    const std::string& what,
    bool all,
    bool tree,
    bool blame) {
  std::set<std::string> w;
  if (!what.empty())
    w.insert(what);
  TargetDescBuilder b(target, w, all, tree, blame);
  return b.BuildDescription();
}

std::unique_ptr<base::DictionaryValue> DescBuilder::DescriptionForConfig(
    const Config* config,
    const std::string& what) {
  std::set<std::string> w;
  if (!what.empty())
    w.insert(what);
  ConfigDescBuilder b(config, w);
  return b.BuildDescription();
}
