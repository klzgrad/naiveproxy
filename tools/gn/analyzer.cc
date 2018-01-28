// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/analyzer.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "tools/gn/builder.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/loader.h"
#include "tools/gn/location.h"
#include "tools/gn/source_file.h"
#include "tools/gn/target.h"

using LabelSet = Analyzer::LabelSet;
using SourceFileSet = Analyzer::SourceFileSet;
using TargetSet = Analyzer::TargetSet;

namespace {

struct Inputs {
  std::vector<SourceFile> source_vec;
  std::vector<Label> compile_vec;
  std::vector<Label> test_vec;
  bool compile_included_all = false;
  SourceFileSet source_files;
  LabelSet compile_labels;
  LabelSet test_labels;
};

struct Outputs {
  std::string status;
  std::string error;
  bool compile_includes_all = false;
  LabelSet compile_labels;
  LabelSet test_labels;
  LabelSet invalid_labels;
};

LabelSet LabelsFor(const TargetSet& targets) {
  LabelSet labels;
  for (auto* target : targets)
    labels.insert(target->label());
  return labels;
}

bool AnyBuildFilesWereModified(const SourceFileSet& source_files) {
  for (auto* file : source_files) {
    if (base::EndsWith(file->value(), ".gn", base::CompareCase::SENSITIVE) ||
        base::EndsWith(file->value(), ".gni", base::CompareCase::SENSITIVE) ||
        base::EndsWith(file->value(), "build/vs_toolchain.py",
                       base::CompareCase::SENSITIVE))
      return true;
  }
  return false;
}

TargetSet Intersect(const TargetSet& l, const TargetSet& r) {
  TargetSet result;
  std::set_intersection(l.begin(), l.end(), r.begin(), r.end(),
                        std::inserter(result, result.begin()));
  return result;
}

std::vector<std::string> GetStringVector(const base::DictionaryValue& dict,
                                         const std::string& key,
                                         Err* err) {
  std::vector<std::string> strings;
  const base::ListValue* lst;
  bool ret = dict.GetList(key, &lst);
  if (!ret) {
    *err = Err(Location(), "Input does not have a key named \"" + key +
                               "\" with a list value.");
    return strings;
  }

  for (size_t i = 0; i < lst->GetSize(); i++) {
    std::string s;
    ret = lst->GetString(i, &s);
    if (!ret) {
      *err = Err(Location(), "Item " + std::to_string(i) + " of \"" + key +
                                 "\" is not a string.");
      strings.clear();
      return strings;
    }
    strings.push_back(std::move(s));
  }
  *err = Err();
  return strings;
}

void WriteString(base::DictionaryValue& dict,
                 const std::string& key,
                 const std::string& value) {
  dict.SetKey(key, base::Value(value));
};

void WriteLabels(const Label& default_toolchain,
                 base::DictionaryValue& dict,
                 const std::string& key,
                 const LabelSet& labels) {
  std::vector<std::string> strings;
  auto value = base::MakeUnique<base::ListValue>();
  for (const auto l : labels)
    strings.push_back(l.GetUserVisibleName(default_toolchain));
  std::sort(strings.begin(), strings.end());
  value->AppendStrings(strings);
  dict.SetWithoutPathExpansion(key, std::move(value));
}

Label AbsoluteOrSourceAbsoluteStringToLabel(const Label& default_toolchain,
                                            const std::string& s, Err* err) {
  if (!IsPathSourceAbsolute(s) && !IsPathAbsolute(s)) {
    *err = Err(Location(),
               "\"" + s + "\" is not a source-absolute or absolute path.");
    return Label();
  }
  return Label::Resolve(SourceDir("//"), default_toolchain, Value(nullptr, s),
                        err);
}

Err JSONToInputs(const Label& default_toolchain,
                 const std::string input,
                 Inputs* inputs) {
  int error_code_out;
  std::string error_msg_out;
  int error_line_out;
  int error_column_out;
  std::unique_ptr<base::Value> value = base::JSONReader().ReadAndReturnError(
      input, base::JSONParserOptions::JSON_PARSE_RFC, &error_code_out,
      &error_msg_out, &error_line_out, &error_column_out);
  if (!value)
    return Err(Location(), "Input is not valid JSON:" + error_msg_out);

  const base::DictionaryValue* dict;
  if (!value->GetAsDictionary(&dict))
    return Err(Location(), "Input is not a dictionary.");

  Err err;
  std::vector<std::string> strings;
  strings = GetStringVector(*dict, "files", &err);
  if (err.has_error())
    return err;
  for (auto s : strings) {
    if (!IsPathSourceAbsolute(s) && !IsPathAbsolute(s))
      return Err(Location(),
                 "\"" + s + "\" is not a source-absolute or absolute path.");
    inputs->source_vec.push_back(SourceFile(s));
  }

  strings = GetStringVector(*dict, "additional_compile_targets", &err);
  if (err.has_error())
    return err;

  inputs->compile_included_all = false;
  for (auto& s : strings) {
    if (s == "all") {
      inputs->compile_included_all = true;
    } else {
      inputs->compile_vec.push_back(
          AbsoluteOrSourceAbsoluteStringToLabel(default_toolchain, s, &err));
      if (err.has_error())
        return err;
    }
  }

  strings = GetStringVector(*dict, "test_targets", &err);
  if (err.has_error())
    return err;
  for (auto& s : strings) {
    inputs->test_vec.push_back(
        AbsoluteOrSourceAbsoluteStringToLabel(default_toolchain, s, &err));
    if (err.has_error())
      return err;
  }

  for (auto& s : inputs->source_vec)
    inputs->source_files.insert(&s);
  for (auto& l : inputs->compile_vec)
    inputs->compile_labels.insert(l);
  for (auto& l : inputs->test_vec)
    inputs->test_labels.insert(l);
  return Err();
}

std::string OutputsToJSON(const Outputs& outputs,
                          const Label& default_toolchain, Err *err) {
  std::string output;
  auto value = base::MakeUnique<base::DictionaryValue>();

  if (outputs.error.size()) {
    WriteString(*value, "error", outputs.error);
    WriteLabels(default_toolchain, *value, "invalid_targets",
                outputs.invalid_labels);
  } else {
    WriteString(*value, "status", outputs.status);
    if (outputs.compile_includes_all) {
      auto compile_targets = base::MakeUnique<base::ListValue>();
      compile_targets->AppendString("all");
      value->SetWithoutPathExpansion("compile_targets",
                                     std::move(compile_targets));
    } else {
      WriteLabels(default_toolchain, *value, "compile_targets",
                  outputs.compile_labels);
    }
    WriteLabels(default_toolchain, *value, "test_targets", outputs.test_labels);
  }

  if (!base::JSONWriter::Write(*value.get(), &output))
    *err = Err(Location(), "Failed to marshal JSON value for output");
  return output;
}

}  // namespace

Analyzer::Analyzer(const Builder& builder)
    : all_targets_(builder.GetAllResolvedTargets()),
      default_toolchain_(builder.loader()->GetDefaultToolchain()) {
  for (const auto* target : all_targets_) {
    labels_to_targets_[target->label()] = target;
    for (const auto& dep_pair : target->GetDeps(Target::DEPS_ALL))
      dep_map_.insert(std::make_pair(dep_pair.ptr, target));
  }
  for (const auto* target : all_targets_) {
    if (dep_map_.find(target) == dep_map_.end())
      roots_.insert(target);
  }
}

Analyzer::~Analyzer() {}

std::string Analyzer::Analyze(const std::string& input, Err* err) const {
  Inputs inputs;
  Outputs outputs;

  Err local_err = JSONToInputs(default_toolchain_, input, &inputs);
  if (local_err.has_error()) {
    outputs.error = local_err.message();
    return OutputsToJSON(outputs, default_toolchain_, err);
  }

  LabelSet invalid_labels;
  for (const auto& label : InvalidLabels(inputs.compile_labels))
    invalid_labels.insert(label);
  for (const auto& label : InvalidLabels(inputs.test_labels))
    invalid_labels.insert(label);
  if (!invalid_labels.empty()) {
    outputs.error = "Invalid targets";
    outputs.invalid_labels = invalid_labels;
    return OutputsToJSON(outputs, default_toolchain_, err);
  }

  // TODO(crbug.com/555273): We can do smarter things when we detect changes
  // to build files. For example, if all of the ninja files are unchanged,
  // we know that we can ignore changes to .gn* files. Also, for most .gn
  // files, we can treat a change as simply affecting every target, config,
  // or toolchain defined in that file.
  if (AnyBuildFilesWereModified(inputs.source_files)) {
    outputs.status = "Found dependency (all)";
    if (inputs.compile_included_all) {
      outputs.compile_includes_all = true;
    } else {
      outputs.compile_labels.insert(inputs.compile_labels.begin(),
                                    inputs.compile_labels.end());
      outputs.compile_labels.insert(inputs.test_labels.begin(),
                                    inputs.test_labels.end());
    }
    outputs.test_labels = inputs.test_labels;
    return OutputsToJSON(outputs, default_toolchain_, err);
  }

  TargetSet affected_targets = AllAffectedTargets(inputs.source_files);
  if (affected_targets.empty()) {
    outputs.status = "No dependency";
    return OutputsToJSON(outputs, default_toolchain_, err);
  }

  TargetSet compile_targets = TargetsFor(inputs.compile_labels);
  if (inputs.compile_included_all) {
    for (auto* root : roots_)
      compile_targets.insert(root);
  }
  TargetSet filtered_targets = Filter(compile_targets);
  outputs.compile_labels =
      LabelsFor(Intersect(filtered_targets, affected_targets));

  TargetSet test_targets = TargetsFor(inputs.test_labels);
  outputs.test_labels = LabelsFor(Intersect(test_targets, affected_targets));

  if (outputs.compile_labels.empty() && outputs.test_labels.empty())
    outputs.status = "No dependency";
  else
    outputs.status = "Found dependency";
  return OutputsToJSON(outputs, default_toolchain_, err);
}

TargetSet Analyzer::AllAffectedTargets(
    const SourceFileSet& source_files) const {
  TargetSet direct_matches;
  for (auto* source_file : source_files)
    AddTargetsDirectlyReferringToFileTo(source_file, &direct_matches);
  TargetSet all_matches;
  for (auto* match : direct_matches)
    AddAllRefsTo(match, &all_matches);
  return all_matches;
}

LabelSet Analyzer::InvalidLabels(const LabelSet& labels) const {
  LabelSet invalid_labels;
  for (const Label& label : labels) {
    if (labels_to_targets_.find(label) == labels_to_targets_.end())
      invalid_labels.insert(label);
  }
  return invalid_labels;
}

TargetSet Analyzer::TargetsFor(const LabelSet& labels) const {
  TargetSet targets;
  for (const auto& label : labels) {
    auto it = labels_to_targets_.find(label);
    if (it != labels_to_targets_.end())
      targets.insert(it->second);
  }
  return targets;
}

TargetSet Analyzer::Filter(const TargetSet& targets) const {
  TargetSet seen;
  TargetSet filtered;
  for (const auto* target : targets)
    FilterTarget(target, &seen, &filtered);
  return filtered;
}

void Analyzer::FilterTarget(const Target* target,
                            TargetSet* seen,
                            TargetSet* filtered) const {
  if (seen->find(target) == seen->end()) {
    seen->insert(target);
    if (target->output_type() != Target::GROUP) {
      filtered->insert(target);
    } else {
      for (const auto& pair : target->GetDeps(Target::DEPS_ALL))
        FilterTarget(pair.ptr, seen, filtered);
    }
  }
}

bool Analyzer::TargetRefersToFile(const Target* target,
                                  const SourceFile* file) const {
  for (const auto& cur_file : target->sources()) {
    if (cur_file == *file)
      return true;
  }
  for (const auto& cur_file : target->public_headers()) {
    if (cur_file == *file)
      return true;
  }
  for (const auto& cur_file : target->inputs()) {
    if (cur_file == *file)
      return true;
  }
  for (const auto& cur_file : target->data()) {
    if (cur_file == file->value())
      return true;
    if (cur_file.back() == '/' &&
        base::StartsWith(file->value(), cur_file, base::CompareCase::SENSITIVE))
      return true;
  }

  if (target->action_values().script().value() == file->value())
    return true;

  std::vector<SourceFile> outputs;
  target->action_values().GetOutputsAsSourceFiles(target, &outputs);
  for (const auto& cur_file : outputs) {
    if (cur_file == *file)
      return true;
  }
  return false;
}

void Analyzer::AddTargetsDirectlyReferringToFileTo(const SourceFile* file,
                                                   TargetSet* matches) const {
  for (auto* target : all_targets_) {
    // Only handles targets in the default toolchain.
    if ((target->label().GetToolchainLabel() == default_toolchain_) &&
        TargetRefersToFile(target, file))
      matches->insert(target);
  }
}

void Analyzer::AddAllRefsTo(const Target* target, TargetSet* results) const {
  if (results->find(target) != results->end())
    return;  // Already found this target.
  results->insert(target);

  auto dep_begin = dep_map_.lower_bound(target);
  auto dep_end = dep_map_.upper_bound(target);
  for (auto cur_dep = dep_begin; cur_dep != dep_end; cur_dep++)
    AddAllRefsTo(cur_dep->second, results);
}
