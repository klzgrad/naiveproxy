// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/err.h"
#include "tools/gn/functions.h"
#include "tools/gn/parse_node_value_adapter.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"

namespace functions {

const char kForEach[] = "foreach";
const char kForEach_HelpShort[] =
    "foreach: Iterate over a list.";
const char kForEach_Help[] =
    R"(foreach: Iterate over a list.

    foreach(<loop_var>, <list>) {
      <loop contents>
    }

  Executes the loop contents block over each item in the list, assigning the
  loop_var to each item in sequence. The loop_var will be a copy so assigning
  to it will not mutate the list.

  The block does not introduce a new scope, so that variable assignments inside
  the loop will be visible once the loop terminates.

  The loop variable will temporarily shadow any existing variables with the
  same name for the duration of the loop. After the loop terminates the loop
  variable will no longer be in scope, and the previous value (if any) will be
  restored.

Example

  mylist = [ "a", "b", "c" ]
  foreach(i, mylist) {
    print(i)
  }

  Prints:
  a
  b
  c
)";

Value RunForEach(Scope* scope,
                 const FunctionCallNode* function,
                 const ListNode* args_list,
                 Err* err) {
  const auto& args_vector = args_list->contents();
  if (args_vector.size() != 2) {
    *err = Err(function, "Wrong number of arguments to foreach().",
               "Expecting exactly two.");
    return Value();
  }

  // Extract the loop variable.
  const IdentifierNode* identifier = args_vector[0]->AsIdentifier();
  if (!identifier) {
    *err =
        Err(args_vector[0].get(), "Expected an identifier for the loop var.");
    return Value();
  }
  base::StringPiece loop_var(identifier->value().value());

  // Extract the list to iterate over.
  ParseNodeValueAdapter list_adapter;
  if (!list_adapter.InitForType(scope, args_vector[1].get(), Value::LIST, err))
    return Value();
  const std::vector<Value>& list = list_adapter.get().list_value();

  // Block to execute.
  const BlockNode* block = function->block();
  if (!block) {
    *err = Err(function, "Expected { after foreach.");
    return Value();
  }

  // If the loop variable was previously defined in this scope, save it so we
  // can put it back after the loop is done.
  const Value* old_loop_value_ptr = scope->GetValue(loop_var);
  Value old_loop_value;
  if (old_loop_value_ptr)
    old_loop_value = *old_loop_value_ptr;

  for (const auto& cur : list) {
    scope->SetValue(loop_var, cur, function);
    block->Execute(scope, err);
    if (err->has_error())
      return Value();
  }

  // Put back loop var.
  if (old_loop_value_ptr) {
    // Put back old value. Use the copy we made, rather than use the pointer,
    // which will probably point to the new value now in the scope.
    scope->SetValue(loop_var, std::move(old_loop_value),
                    old_loop_value.origin());
  } else {
    // Loop variable was undefined before loop, delete it.
    scope->RemoveIdentifier(loop_var);
  }

  return Value();
}

}  // namespace functions
