// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_CREATE_BUNDLE_TARGET_GENERATOR_H_
#define TOOLS_GN_CREATE_BUNDLE_TARGET_GENERATOR_H_

#include "base/macros.h"
#include "tools/gn/target_generator.h"

class SourceDir;

// Populates a Target with the values from a create_bundle rule.
class CreateBundleTargetGenerator : public TargetGenerator {
 public:
  CreateBundleTargetGenerator(Target* target,
                              Scope* scope,
                              const FunctionCallNode* function_call,
                              Err* err);
  ~CreateBundleTargetGenerator() override;

 protected:
  void DoRun() override;

 private:
  bool FillBundleDir(const SourceDir& bundle_root_dir,
                     const base::StringPiece& name,
                     SourceDir* bundle_dir);

  bool FillXcodeExtraAttributes();

  bool FillProductType();
  bool FillPartialInfoPlist();
  bool FillXcodeTestApplicationName();

  bool FillCodeSigningScript();
  bool FillCodeSigningSources();
  bool FillCodeSigningOutputs();
  bool FillCodeSigningArgs();
  bool FillBundleDepsFilter();

  DISALLOW_COPY_AND_ASSIGN(CreateBundleTargetGenerator);
};

#endif  // TOOLS_GN_CREATE_BUNDLE_TARGET_GENERATOR_H_
