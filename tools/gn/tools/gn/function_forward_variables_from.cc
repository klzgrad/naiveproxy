// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/err.h"
#include "tools/gn/functions.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"

namespace functions {

namespace {

void ForwardAllValues(const FunctionCallNode* function,
                      Scope* source,
                      Scope* dest,
                      const std::set<std::string>& exclusion_set,
                      Err* err) {
  Scope::MergeOptions options;
  // This function needs to clobber existing for it to be useful. It will be
  // called in a template to forward all values, but there will be some
  // default stuff like configs set up in both scopes, so it would always
  // fail if it didn't clobber.
  options.clobber_existing = true;
  options.skip_private_vars = true;
  options.mark_dest_used = false;
  options.excluded_values = exclusion_set;
  source->NonRecursiveMergeTo(dest, options, function, "source scope", err);
  source->MarkAllUsed();
}

void ForwardValuesFromList(Scope* source,
                           Scope* dest,
                           const std::vector<Value>& list,
                           const std::set<std::string>& exclusion_set,
                           Err* err) {
  for (const Value& cur : list) {
    if (!cur.VerifyTypeIs(Value::STRING, err))
      return;
    if (exclusion_set.find(cur.string_value()) != exclusion_set.end())
      continue;
    const Value* value = source->GetValue(cur.string_value(), true);
    if (value) {
      // Use the storage key for the original value rather than the string in
      // "cur" because "cur" is a temporary that will be deleted, and Scopes
      // expect a persistent StringPiece (it won't copy). Not doing this will
      // lead the scope's key to point to invalid memory after this returns.
      base::StringPiece storage_key = source->GetStorageKey(cur.string_value());
      if (storage_key.empty()) {
        // Programmatic value, don't allow copying.
        *err =
            Err(cur, "This value can't be forwarded.",
                "The variable \"" + cur.string_value() + "\" is a built-in.");
        return;
      }

      // Don't allow clobbering existing values.
      const Value* existing_value = dest->GetValue(storage_key);
      if (existing_value) {
        *err = Err(
            cur, "Clobbering existing value.",
            "The current scope already defines a value \"" +
                cur.string_value() +
                "\".\nforward_variables_from() won't clobber "
                "existing values. If you want to\nmerge lists, you'll need to "
                "do this explicitly.");
        err->AppendSubErr(Err(*existing_value, "value being clobbered."));
        return;
      }

      // Keep the origin information from the original value. The normal
      // usage is for this to be used in a template, and if there's an error,
      // the user expects to see the line where they set the variable
      // blamed, rather than a template call to forward_variables_from().
      dest->SetValue(storage_key, *value, value->origin());
    }
  }
}

}  // namespace

const char kForwardVariablesFrom[] = "forward_variables_from";
const char kForwardVariablesFrom_HelpShort[] =
    "forward_variables_from: Copies variables from a different scope.";
const char kForwardVariablesFrom_Help[] =
    R"(forward_variables_from: Copies variables from a different scope.

  forward_variables_from(from_scope, variable_list_or_star,
                         variable_to_not_forward_list = [])

  Copies the given variables from the given scope to the local scope if they
  exist. This is normally used in the context of templates to use the values of
  variables defined in the template invocation to a template-defined target.

  The variables in the given variable_list will be copied if they exist in the
  given scope or any enclosing scope. If they do not exist, nothing will happen
  and they be left undefined in the current scope.

  As a special case, if the variable_list is a string with the value of "*",
  all variables from the given scope will be copied. "*" only copies variables
  set directly on the from_scope, not enclosing ones. Otherwise it would
  duplicate all global variables.

  When an explicit list of variables is supplied, if the variable exists in the
  current (destination) scope already, an error will be thrown. If "*" is
  specified, variables in the current scope will be clobbered (the latter is
  important because most targets have an implicit configs list, which means it
  wouldn't work at all if it didn't clobber).

  The sources assignment filter (see "gn help set_sources_assignment_filter")
  is never applied by this function. It's assumed than any desired filtering
  was already done when sources was set on the from_scope.

  If variables_to_not_forward_list is non-empty, then it must contains a list
  of variable names that will not be forwarded. This is mostly useful when
  variable_list_or_star has a value of "*".

Examples

  # forward_variables_from(invoker, ["foo"])
  # is equivalent to:
  assert(!defined(foo))
  if (defined(invoker.foo)) {
    foo = invoker.foo
  }

  # This is a common action template. It would invoke a script with some given
  # parameters, and wants to use the various types of deps and the visibility
  # from the invoker if it's defined. It also injects an additional dependency
  # to all targets.
  template("my_test") {
    action(target_name) {
      forward_variables_from(invoker, [ "data_deps", "deps",
                                        "public_deps", "visibility"])
      # Add our test code to the dependencies.
      # "deps" may or may not be defined at this point.
      if (defined(deps)) {
        deps += [ "//tools/doom_melon" ]
      } else {
        deps = [ "//tools/doom_melon" ]
      }
    }
  }

  # This is a template around a target whose type depends on a global variable.
  # It forwards all values from the invoker.
  template("my_wrapper") {
    target(my_wrapper_target_type, target_name) {
      forward_variables_from(invoker, "*")
    }
  }

  # A template that wraps another. It adds behavior based on one
  # variable, and forwards all others to the nested target.
  template("my_ios_test_app") {
    ios_test_app(target_name) {
      forward_variables_from(invoker, "*", ["test_bundle_name"])
      if (!defined(extra_substitutions)) {
        extra_substitutions = []
      }
      extra_substitutions += [ "BUNDLE_ID_TEST_NAME=$test_bundle_name" ]
    }
  }
)";

// This function takes a ListNode rather than a resolved vector of values
// both avoid copying the potentially-large source scope, and so the variables
// in the source scope can be marked as used.
Value RunForwardVariablesFrom(Scope* scope,
                              const FunctionCallNode* function,
                              const ListNode* args_list,
                              Err* err) {
  const auto& args_vector = args_list->contents();
  if (args_vector.size() != 2 && args_vector.size() != 3) {
    *err = Err(function, "Wrong number of arguments.",
               "Expecting two or three arguments.");
    return Value();
  }

  Value* value = nullptr;  // Value to use, may point to result_value.
  Value result_value;      // Storage for the "evaluate" case.
  const IdentifierNode* identifier = args_vector[0]->AsIdentifier();
  if (identifier) {
    // Optimize the common case where the input scope is an identifier. This
    // prevents a copy of a potentially large Scope object.
    value = scope->GetMutableValue(identifier->value().value(),
                                   Scope::SEARCH_NESTED, true);
    if (!value) {
      *err = Err(identifier, "Undefined identifier.");
      return Value();
    }
  } else {
    // Non-optimized case, just evaluate the argument.
    result_value = args_vector[0]->Execute(scope, err);
    if (err->has_error())
      return Value();
    value = &result_value;
  }

  // Extract the source scope.
  if (!value->VerifyTypeIs(Value::SCOPE, err))
    return Value();
  Scope* source = value->scope_value();

  // Extract the exclusion list if defined.
  std::set<std::string> exclusion_set;
  if (args_vector.size() == 3) {
    Value exclusion_value = args_vector[2]->Execute(scope, err);
    if (err->has_error())
      return Value();

    if (exclusion_value.type() != Value::LIST) {
      *err = Err(exclusion_value, "Not a valid list of variables to exclude.",
                 "Expecting a list of strings.");
      return Value();
    }

    for (const Value& cur : exclusion_value.list_value()) {
      if (!cur.VerifyTypeIs(Value::STRING, err))
        return Value();

      exclusion_set.insert(cur.string_value());
    }
  }

  // Extract the list. If all_values is not set, the what_value will be a list.
  Value what_value = args_vector[1]->Execute(scope, err);
  if (err->has_error())
    return Value();
  if (what_value.type() == Value::STRING) {
    if (what_value.string_value() == "*") {
      ForwardAllValues(function, source, scope, exclusion_set, err);
      return Value();
    }
  } else {
    if (what_value.type() == Value::LIST) {
      ForwardValuesFromList(source, scope, what_value.list_value(),
                            exclusion_set, err);
      return Value();
    }
  }

  // Not the right type of argument.
  *err = Err(what_value, "Not a valid list of variables to copy.",
             "Expecting either the string \"*\" or a list of strings.");
  return Value();
}

}  // namespace functions
