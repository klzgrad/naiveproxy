// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/bundle_data_target_generator.h"

#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"
#include "tools/gn/substitution_type.h"
#include "tools/gn/target.h"
#include "tools/gn/value.h"
#include "tools/gn/variables.h"

BundleDataTargetGenerator::BundleDataTargetGenerator(
    Target* target,
    Scope* scope,
    const FunctionCallNode* function_call,
    Err* err) : TargetGenerator(target, scope, function_call, err) {}

BundleDataTargetGenerator::~BundleDataTargetGenerator() {}

void BundleDataTargetGenerator::DoRun() {
  target_->set_output_type(Target::BUNDLE_DATA);

  if (!FillSources())
    return;
  if (!FillOutputs())
    return;

  if (target_->sources().empty()) {
    *err_ = Err(function_call_, "Empty sources for bundle_data target."
        "You have to specify at least one file in the \"sources\".");
    return;
  }
  if (target_->action_values().outputs().list().size() != 1) {
    *err_ = Err(function_call_,
        "Target bundle_data must have exactly one ouput.",
        "You must specify exactly one value in the \"output\" array for the"
        "destination\ninto the generated bundle (see \"gn help bundle_data\"). "
        "If there are multiple\nsources to copy, use source expansion (see "
        "\"gn help source_expansion\").");
    return;
  }
}

bool BundleDataTargetGenerator::FillOutputs() {
  const Value* value = scope_->GetValue(variables::kOutputs, true);
  if (!value)
    return true;

  SubstitutionList& outputs = target_->action_values().outputs();
  if (!outputs.Parse(*value, err_))
    return false;

  // Check the substitutions used are valid for this purpose.
  for (SubstitutionType type : outputs.required_types()) {
    if (!IsValidBundleDataSubstitution(type)) {
      *err_ = Err(value->origin(), "Invalid substitution type.",
          "The substitution " + std::string(kSubstitutionNames[type]) +
          " isn't valid for something\n"
          "operating on a bundle_data file such as this.");
      return false;
    }
  }

  // Validate that outputs are in the bundle.
  CHECK(outputs.list().size() == value->list_value().size());
  for (size_t i = 0; i < outputs.list().size(); i++) {
    if (!EnsureSubstitutionIsInBundleDir(outputs.list()[i],
                                         value->list_value()[i]))
      return false;
  }

  return true;
}

bool BundleDataTargetGenerator::EnsureSubstitutionIsInBundleDir(
    const SubstitutionPattern& pattern,
    const Value& original_value) {
  if (pattern.ranges().empty()) {
    // Pattern is empty, error out (this prevents weirdness below).
    *err_ = Err(original_value, "This has an empty value in it.");
    return false;
  }

  if (SubstitutionIsInBundleDir(pattern.ranges()[0].type))
    return true;

  *err_ = Err(original_value,
      "File is not inside bundle directory.",
      "The given file should be in the output directory. Normally you\n"
      "would specify {{bundle_resources_dir}} or such substitution.");
  return false;
}
