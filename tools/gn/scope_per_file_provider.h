// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SCOPE_PER_FILE_PROVIDER_H_
#define TOOLS_GN_SCOPE_PER_FILE_PROVIDER_H_

#include <memory>

#include "base/macros.h"
#include "tools/gn/scope.h"

// ProgrammaticProvider for a scope to provide it with per-file built-in
// variable support.
class ScopePerFileProvider : public Scope::ProgrammaticProvider {
 public:
  // allow_target_vars allows the target-related variables to get resolved.
  // When allow_target_vars is unset, the target-related values will be
  // undefined to GN script.
  ScopePerFileProvider(Scope* scope, bool allow_target_vars);
  ~ScopePerFileProvider() override;

  // ProgrammaticProvider implementation.
  const Value* GetProgrammaticValue(const base::StringPiece& ident) override;

 private:
  const Value* GetCurrentToolchain();
  const Value* GetDefaultToolchain();
  const Value* GetPythonPath();
  const Value* GetRootBuildDir();
  const Value* GetRootGenDir();
  const Value* GetRootOutDir();
  const Value* GetTargetGenDir();
  const Value* GetTargetOutDir();

  bool allow_target_vars_;

  // All values are lazily created.
  std::unique_ptr<Value> current_toolchain_;
  std::unique_ptr<Value> default_toolchain_;
  std::unique_ptr<Value> python_path_;
  std::unique_ptr<Value> root_build_dir_;
  std::unique_ptr<Value> root_gen_dir_;
  std::unique_ptr<Value> root_out_dir_;
  std::unique_ptr<Value> target_gen_dir_;
  std::unique_ptr<Value> target_out_dir_;

  DISALLOW_COPY_AND_ASSIGN(ScopePerFileProvider);
};

#endif  // TOOLS_GN_SCOPE_PER_FILE_PROVIDER_H_
