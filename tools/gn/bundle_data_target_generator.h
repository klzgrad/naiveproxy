// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_BUNDLE_DATA_TARGET_GENERATOR_H_
#define TOOLS_GN_BUNDLE_DATA_TARGET_GENERATOR_H_

#include "base/macros.h"
#include "tools/gn/target_generator.h"

// Populates a Target with the values from a bundle_data rule.
class BundleDataTargetGenerator : public TargetGenerator {
 public:
  BundleDataTargetGenerator(Target* target,
                            Scope* scope,
                            const FunctionCallNode* function_call,
                            Err* err);
  ~BundleDataTargetGenerator() override;

 protected:
  void DoRun() override;

 private:
  bool FillOutputs();

  bool EnsureSubstitutionIsInBundleDir(
      const SubstitutionPattern& pattern,
      const Value& original_value);

  DISALLOW_COPY_AND_ASSIGN(BundleDataTargetGenerator);
};

#endif  // TOOLS_GN_BUNDLE_DATA_TARGET_GENERATOR_H_
