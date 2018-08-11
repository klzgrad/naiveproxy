// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/action_target_generator.h"

#include "base/stl_util.h"
#include "tools/gn/build_settings.h"
#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/functions.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"
#include "tools/gn/value.h"
#include "tools/gn/value_extractors.h"
#include "tools/gn/variables.h"

ActionTargetGenerator::ActionTargetGenerator(
    Target* target,
    Scope* scope,
    const FunctionCallNode* function_call,
    Target::OutputType type,
    Err* err)
    : TargetGenerator(target, scope, function_call, err),
      output_type_(type) {
}

ActionTargetGenerator::~ActionTargetGenerator() = default;

void ActionTargetGenerator::DoRun() {
  target_->set_output_type(output_type_);

  if (!FillSources())
    return;
  if (output_type_ == Target::ACTION_FOREACH && target_->sources().empty()) {
    // Foreach rules must always have some sources to have an effect.
    *err_ = Err(function_call_, "action_foreach target has no sources.",
        "If you don't specify any sources, there is nothing to run your\n"
        "script over.");
    return;
  }

  if (!FillInputs())
    return;

  if (!FillScript())
    return;

  if (!FillScriptArgs())
    return;

  if (!FillResponseFileContents())
    return;

  if (!FillOutputs(output_type_ == Target::ACTION_FOREACH))
    return;

  if (!FillDepfile())
    return;

  if (!FillPool())
    return;

  if (!FillCheckIncludes())
    return;

  if (!CheckOutputs())
    return;

  // Action outputs don't depend on the current toolchain so we can skip adding
  // that dependency.

  // response_file_contents and {{response_file_name}} in the args must go
  // together.
  const auto& required_args_substitutions =
      target_->action_values().args().required_types();
  bool has_rsp_file_name = base::ContainsValue(required_args_substitutions,
                                               SUBSTITUTION_RSP_FILE_NAME);
  if (target_->action_values().uses_rsp_file() && !has_rsp_file_name) {
    *err_ = Err(function_call_, "Missing {{response_file_name}} in args.",
        "This target defines response_file_contents but doesn't use\n"
        "{{response_file_name}} in the args, which means the response file\n"
        "will be unused.");
    return;
  }
  if (!target_->action_values().uses_rsp_file() && has_rsp_file_name) {
    *err_ = Err(function_call_, "Missing response_file_contents definition.",
        "This target uses {{response_file_name}} in the args, but does not\n"
        "define response_file_contents which means the response file\n"
        "will be empty.");
    return;
  }
}

bool ActionTargetGenerator::FillScript() {
  // If this gets called, the target type requires a script, so error out
  // if it doesn't have one.
  const Value* value = scope_->GetValue(variables::kScript, true);
  if (!value) {
    *err_ = Err(function_call_, "This target type requires a \"script\".");
    return false;
  }
  if (!value->VerifyTypeIs(Value::STRING, err_))
    return false;

  SourceFile script_file =
      scope_->GetSourceDir().ResolveRelativeFile(
          *value, err_,
          scope_->settings()->build_settings()->root_path_utf8());
  if (err_->has_error())
    return false;
  target_->action_values().set_script(script_file);
  return true;
}

bool ActionTargetGenerator::FillScriptArgs() {
  const Value* value = scope_->GetValue(variables::kArgs, true);
  if (!value)
    return true;  // Nothing to do.

  if (!target_->action_values().args().Parse(*value, err_))
    return false;
  if (!EnsureValidSubstitutions(
           target_->action_values().args().required_types(),
           &IsValidScriptArgsSubstitution,
           value->origin(), err_))
    return false;

  return true;
}

bool ActionTargetGenerator::FillResponseFileContents() {
  const Value* value = scope_->GetValue(variables::kResponseFileContents, true);
  if (!value)
    return true;  // Nothing to do.

  if (!target_->action_values().rsp_file_contents().Parse(*value, err_))
    return false;
  if (!EnsureValidSubstitutions(
           target_->action_values().rsp_file_contents().required_types(),
           &IsValidSourceSubstitution, value->origin(), err_))
    return false;

  return true;
}

bool ActionTargetGenerator::FillDepfile() {
  const Value* value = scope_->GetValue(variables::kDepfile, true);
  if (!value)
    return true;

  SubstitutionPattern depfile;
  if (!depfile.Parse(*value, err_))
    return false;
  if (!EnsureSubstitutionIsInOutputDir(depfile, *value))
    return false;

  target_->action_values().set_depfile(depfile);
  return true;
}

bool ActionTargetGenerator::FillPool() {
  const Value* value = scope_->GetValue(variables::kPool, true);
  if (!value)
    return true;

  Label label = Label::Resolve(scope_->GetSourceDir(),
                               ToolchainLabelForScope(scope_), *value, err_);
  if (err_->has_error())
    return false;

  LabelPtrPair<Pool> pair(label);
  pair.origin = target_->defined_from();

  target_->action_values().set_pool(std::move(pair));
  return true;
}

bool ActionTargetGenerator::CheckOutputs() {
  const SubstitutionList& outputs = target_->action_values().outputs();
  if (outputs.list().empty()) {
    *err_ = Err(function_call_, "Action has no outputs.",
        "If you have no outputs, the build system can not tell when your\n"
        "script needs to be run.");
    return false;
  }

  if (output_type_ == Target::ACTION) {
    if (!outputs.required_types().empty()) {
      *err_ = Err(function_call_, "Action has patterns in the output.",
          "An action target should have the outputs completely specified. If\n"
          "you want to provide a mapping from source to output, use an\n"
          "\"action_foreach\" target.");
      return false;
    }
  } else if (output_type_ == Target::ACTION_FOREACH) {
    // A foreach target should always have a pattern in the outputs.
    if (outputs.required_types().empty()) {
      *err_ = Err(function_call_,
          "action_foreach should have a pattern in the output.",
          "An action_foreach target should have a source expansion pattern in\n"
          "it to map source file to unique output file name. Otherwise, the\n"
          "build system can't determine when your script needs to be run.");
      return false;
    }
  }
  return true;
}

bool ActionTargetGenerator::FillInputs() {
  const Value* value = scope_->GetValue(variables::kInputs, true);
  if (!value)
    return true;

  Target::FileList dest_inputs;
  if (!ExtractListOfRelativeFiles(scope_->settings()->build_settings(), *value,
                                  scope_->GetSourceDir(), &dest_inputs, err_))
    return false;
  target_->config_values().inputs().swap(dest_inputs);
  return true;
}
