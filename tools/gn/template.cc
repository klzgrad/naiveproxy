// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/template.h"

#include <utility>

#include "tools/gn/err.h"
#include "tools/gn/functions.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"
#include "tools/gn/scope_per_file_provider.h"
#include "tools/gn/value.h"
#include "tools/gn/variables.h"

Template::Template(const Scope* scope, const FunctionCallNode* def)
    : closure_(scope->MakeClosure()),
      definition_(def) {
}

Template::Template(std::unique_ptr<Scope> scope, const FunctionCallNode* def)
    : closure_(std::move(scope)), definition_(def) {}

Template::~Template() {
}

Value Template::Invoke(Scope* scope,
                       const FunctionCallNode* invocation,
                       const std::string& template_name,
                       const std::vector<Value>& args,
                       BlockNode* block,
                       Err* err) const {
  // Don't allow templates to be executed from imported files. Imports are for
  // simple values only.
  if (!EnsureNotProcessingImport(invocation, scope, err))
    return Value();

  // First run the invocation's block. Need to allocate the scope on the heap
  // so we can pass ownership to the template.
  std::unique_ptr<Scope> invocation_scope(new Scope(scope));
  if (!FillTargetBlockScope(scope, invocation, template_name,
                            block, args, invocation_scope.get(), err))
    return Value();

  {
    // Don't allow the block of the template invocation to include other
    // targets configs, or template invocations. This must only be applied
    // to the invoker's block rather than the whole function because the
    // template execution itself must be able to define targets, etc.
    NonNestableBlock non_nestable(scope, invocation, "template invocation");
    if (!non_nestable.Enter(err))
      return Value();

    block->Execute(invocation_scope.get(), err);
    if (err->has_error())
      return Value();
  }

  // Set up the scope to run the template and set the current directory for the
  // template (which ScopePerFileProvider uses to base the target-related
  // variables target_gen_dir and target_out_dir on) to be that of the invoker.
  // This way, files don't have to be rebased and target_*_dir works the way
  // people expect (otherwise its to easy to be putting generated files in the
  // gen dir corresponding to an imported file).
  Scope template_scope(closure_.get());
  template_scope.set_source_dir(scope->GetSourceDir());

  ScopePerFileProvider per_file_provider(&template_scope, true);

  // Targets defined in the template go in the collector for the invoking file.
  template_scope.set_item_collector(scope->GetItemCollector());

  // We jump through some hoops to avoid copying the invocation scope when
  // setting it in the template scope (since the invocation scope may have
  // large lists of source files in it and could be expensive to copy).
  //
  // Scope.SetValue will copy the value which will in turn copy the scope, but
  // if we instead create a value and then set the scope on it, the copy can
  // be avoided.
  template_scope.SetValue(variables::kInvoker,
                          Value(nullptr, std::unique_ptr<Scope>()), invocation);
  Value* invoker_value = template_scope.GetMutableValue(
      variables::kInvoker, Scope::SEARCH_NESTED, false);
  invoker_value->SetScopeValue(std::move(invocation_scope));
  template_scope.set_source_dir(scope->GetSourceDir());

  const base::StringPiece target_name(variables::kTargetName);
  template_scope.SetValue(target_name,
                          Value(invocation, args[0].string_value()),
                          invocation);

  // Actually run the template code.
  Value result =
      definition_->block()->Execute(&template_scope, err);
  if (err->has_error()) {
    // If there was an error, append the caller location so the error message
    // displays a stack trace of how it got here.
    err->AppendSubErr(Err(invocation, "whence it was called."));
    return Value();
  }

  // Check for unused variables in the invocation scope. This will find typos
  // of things the caller meant to pass to the template but the template didn't
  // read out.
  //
  // This is a bit tricky because it's theoretically possible for the template
  // to overwrite the value of "invoker" and free the Scope owned by the
  // value. So we need to look it up again and don't do anything if it doesn't
  // exist.
  invoker_value = template_scope.GetMutableValue(
      variables::kInvoker, Scope::SEARCH_NESTED, false);
  if (invoker_value && invoker_value->type() == Value::SCOPE) {
    if (!invoker_value->scope_value()->CheckForUnusedVars(err))
      return Value();
  }

  // Check for unused variables in the template itself.
  if (!template_scope.CheckForUnusedVars(err))
    return Value();

  return result;
}

LocationRange Template::GetDefinitionRange() const {
  return definition_->GetRange();
}
