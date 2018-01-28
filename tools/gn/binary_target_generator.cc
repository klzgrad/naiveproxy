// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/binary_target_generator.h"

#include "tools/gn/config_values_generator.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/functions.h"
#include "tools/gn/scope.h"
#include "tools/gn/settings.h"
#include "tools/gn/value_extractors.h"
#include "tools/gn/variables.h"

BinaryTargetGenerator::BinaryTargetGenerator(
    Target* target,
    Scope* scope,
    const FunctionCallNode* function_call,
    Target::OutputType type,
    Err* err)
    : TargetGenerator(target, scope, function_call, err),
      output_type_(type) {
}

BinaryTargetGenerator::~BinaryTargetGenerator() {
}

void BinaryTargetGenerator::DoRun() {
  target_->set_output_type(output_type_);

  if (!FillOutputName())
    return;

  if (!FillOutputPrefixOverride())
    return;

  if (!FillOutputDir())
    return;

  if (!FillOutputExtension())
    return;

  if (!FillSources())
    return;

  if (!FillPublic())
    return;

  if (!FillCheckIncludes())
    return;

  if (!FillInputs())
    return;

  if (!FillConfigs())
    return;

  if (!FillAllowCircularIncludesFrom())
    return;

  if (!FillCompleteStaticLib())
    return;

  // Config values (compiler flags, etc.) set directly on this target.
  ConfigValuesGenerator gen(&target_->config_values(), scope_,
                            scope_->GetSourceDir(), err_);
  gen.Run();
  if (err_->has_error())
    return;
}

bool BinaryTargetGenerator::FillCompleteStaticLib() {
  if (target_->output_type() == Target::STATIC_LIBRARY) {
    const Value* value = scope_->GetValue(variables::kCompleteStaticLib, true);
    if (!value)
      return true;
    if (!value->VerifyTypeIs(Value::BOOLEAN, err_))
      return false;
    target_->set_complete_static_lib(value->boolean_value());
  }
  return true;
}

bool BinaryTargetGenerator::FillOutputName() {
  const Value* value = scope_->GetValue(variables::kOutputName, true);
  if (!value)
    return true;
  if (!value->VerifyTypeIs(Value::STRING, err_))
    return false;
  target_->set_output_name(value->string_value());
  return true;
}

bool BinaryTargetGenerator::FillOutputPrefixOverride() {
  const Value* value = scope_->GetValue(variables::kOutputPrefixOverride, true);
  if (!value)
    return true;
  if (!value->VerifyTypeIs(Value::BOOLEAN, err_))
    return false;
  target_->set_output_prefix_override(value->boolean_value());
  return true;
}

bool BinaryTargetGenerator::FillOutputDir() {
  const Value* value = scope_->GetValue(variables::kOutputDir, true);
  if (!value)
    return true;
  if (!value->VerifyTypeIs(Value::STRING, err_))
    return false;

  if (value->string_value().empty())
    return true;  // Treat empty string as the default and do nothing.

  const BuildSettings* build_settings = scope_->settings()->build_settings();
  SourceDir dir = scope_->GetSourceDir().ResolveRelativeDir(
      *value, err_, build_settings->root_path_utf8());
  if (err_->has_error())
    return false;

  if (!EnsureStringIsInOutputDir(build_settings->build_dir(),
                                 dir.value(), value->origin(), err_))
    return false;
  target_->set_output_dir(dir);
  return true;
}

bool BinaryTargetGenerator::FillOutputExtension() {
  const Value* value = scope_->GetValue(variables::kOutputExtension, true);
  if (!value)
    return true;
  if (!value->VerifyTypeIs(Value::STRING, err_))
    return false;
  target_->set_output_extension(value->string_value());
  return true;
}

bool BinaryTargetGenerator::FillAllowCircularIncludesFrom() {
  const Value* value = scope_->GetValue(
      variables::kAllowCircularIncludesFrom, true);
  if (!value)
    return true;

  UniqueVector<Label> circular;
  ExtractListOfUniqueLabels(*value, scope_->GetSourceDir(),
                            ToolchainLabelForScope(scope_), &circular, err_);
  if (err_->has_error())
    return false;

  // Validate that all circular includes entries are in the deps.
  for (const auto& cur : circular) {
    bool found_dep = false;
    for (const auto& dep_pair : target_->GetDeps(Target::DEPS_LINKED)) {
      if (dep_pair.label == cur) {
        found_dep = true;
        break;
      }
    }
    if (!found_dep) {
      *err_ = Err(*value, "Label not in deps.",
          "The label \"" + cur.GetUserVisibleName(false) +
          "\"\nwas not in the deps of this target. "
          "allow_circular_includes_from only allows\ntargets present in the "
          "deps.");
      return false;
    }
  }

  // Add to the set.
  for (const auto& cur : circular)
    target_->allow_circular_includes_from().insert(cur);
  return true;
}
