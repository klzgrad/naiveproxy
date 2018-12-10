// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_COPY_TARGET_GENERATOR_H_
#define TOOLS_GN_COPY_TARGET_GENERATOR_H_

#include "base/macros.h"
#include "tools/gn/target_generator.h"

// Populates a Target with the values from a copy rule.
class CopyTargetGenerator : public TargetGenerator {
 public:
  CopyTargetGenerator(Target* target,
                      Scope* scope,
                      const FunctionCallNode* function_call,
                      Err* err);
  ~CopyTargetGenerator() override;

 protected:
  void DoRun() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(CopyTargetGenerator);
};

#endif  // TOOLS_GN_COPY_TARGET_GENERATOR_H_
