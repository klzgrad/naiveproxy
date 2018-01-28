// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/functions.h"

#include <stddef.h>
#include <iostream>
#include <utility>

#include "base/environment.h"
#include "base/strings/string_util.h"
#include "tools/gn/config.h"
#include "tools/gn/config_values_generator.h"
#include "tools/gn/err.h"
#include "tools/gn/input_file.h"
#include "tools/gn/parse_node_value_adapter.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/pool.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/scope.h"
#include "tools/gn/settings.h"
#include "tools/gn/template.h"
#include "tools/gn/token.h"
#include "tools/gn/value.h"
#include "tools/gn/value_extractors.h"
#include "tools/gn/variables.h"

namespace {

// Some functions take a {} following them, and some don't. For the ones that
// don't, this is used to verify that the given block node is null and will
// set the error accordingly if it's not. Returns true if the block is null.
bool VerifyNoBlockForFunctionCall(const FunctionCallNode* function,
                                  const BlockNode* block,
                                  Err* err) {
  if (!block)
    return true;

  *err = Err(block, "Unexpected '{'.",
      "This function call doesn't take a {} block following it, and you\n"
      "can't have a {} block that's not connected to something like an if\n"
      "statement or a target declaration.");
  err->AppendRange(function->function().range());
  return false;
}

// This key is set as a scope property on the scope of a declare_args() block,
// in order to prevent reading a variable defined earlier in the same call
// (see `gn help declare_args` for more).
const void *kInDeclareArgsKey = nullptr;

}  // namespace


bool EnsureNotReadingFromSameDeclareArgs(const ParseNode* node,
                                         const Scope* cur_scope,
                                         const Scope* val_scope,
                                         Err* err) {
  // If the value didn't come from a scope at all, we're safe.
  if (!val_scope)
    return true;

  const Scope* val_args_scope = nullptr;
  val_scope->GetProperty(&kInDeclareArgsKey, &val_args_scope);

  const Scope* cur_args_scope = nullptr;
  cur_scope->GetProperty(&kInDeclareArgsKey, &cur_args_scope);
  if (!val_args_scope || !cur_args_scope || (val_args_scope != cur_args_scope))
    return true;

  *err = Err(node,
      "Reading a variable defined in the same declare_args() call.\n"
      "\n"
      "If you need to set the value of one arg based on another, put\n"
      "them in two separate declare_args() calls, one after the other.\n");
  return false;
}

bool EnsureNotProcessingImport(const ParseNode* node,
                               const Scope* scope,
                               Err* err) {
  if (scope->IsProcessingImport()) {
    *err = Err(node, "Not valid from an import.",
        "Imports are for defining defaults, variables, and rules. The\n"
        "appropriate place for this kind of thing is really in a normal\n"
        "BUILD file.");
    return false;
  }
  return true;
}

bool EnsureNotProcessingBuildConfig(const ParseNode* node,
                                    const Scope* scope,
                                    Err* err) {
  if (scope->IsProcessingBuildConfig()) {
    *err = Err(node, "Not valid from the build config.",
        "You can't do this kind of thing from the build config script, "
        "silly!\nPut it in a regular BUILD file.");
    return false;
  }
  return true;
}

bool FillTargetBlockScope(const Scope* scope,
                          const FunctionCallNode* function,
                          const std::string& target_type,
                          const BlockNode* block,
                          const std::vector<Value>& args,
                          Scope* block_scope,
                          Err* err) {
  if (!block) {
    FillNeedsBlockError(function, err);
    return false;
  }

  // Copy the target defaults, if any, into the scope we're going to execute
  // the block in.
  const Scope* default_scope = scope->GetTargetDefaults(target_type);
  if (default_scope) {
    Scope::MergeOptions merge_options;
    merge_options.skip_private_vars = true;
    if (!default_scope->NonRecursiveMergeTo(block_scope, merge_options,
                                            function, "target defaults", err))
      return false;
  }

  // The name is the single argument to the target function.
  if (!EnsureSingleStringArg(function, args, err))
    return false;

  // Set the target name variable to the current target, and mark it used
  // because we don't want to issue an error if the script ignores it.
  const base::StringPiece target_name(variables::kTargetName);
  block_scope->SetValue(target_name, Value(function, args[0].string_value()),
                        function);
  block_scope->MarkUsed(target_name);
  return true;
}

void FillNeedsBlockError(const FunctionCallNode* function, Err* err) {
  *err = Err(function->function(), "This function call requires a block.",
      "The block's \"{\" must be on the same line as the function "
      "call's \")\".");
}

bool EnsureSingleStringArg(const FunctionCallNode* function,
                           const std::vector<Value>& args,
                           Err* err) {
  if (args.size() != 1) {
    *err = Err(function->function(), "Incorrect arguments.",
               "This function requires a single string argument.");
    return false;
  }
  return args[0].VerifyTypeIs(Value::STRING, err);
}

const Label& ToolchainLabelForScope(const Scope* scope) {
  return scope->settings()->toolchain_label();
}

Label MakeLabelForScope(const Scope* scope,
                        const FunctionCallNode* function,
                        const std::string& name) {
  const Label& toolchain_label = ToolchainLabelForScope(scope);
  return Label(scope->GetSourceDir(), name, toolchain_label.dir(),
               toolchain_label.name());
}

// static
const int NonNestableBlock::kKey = 0;

NonNestableBlock::NonNestableBlock(
    Scope* scope,
    const FunctionCallNode* function,
    const char* type_description)
    : scope_(scope),
      function_(function),
      type_description_(type_description),
      key_added_(false) {
}

NonNestableBlock::~NonNestableBlock() {
  if (key_added_)
    scope_->SetProperty(&kKey, nullptr);
}

bool NonNestableBlock::Enter(Err* err) {
  void* scope_value = scope_->GetProperty(&kKey, nullptr);
  if (scope_value) {
    // Existing block.
    const NonNestableBlock* existing =
        reinterpret_cast<const NonNestableBlock*>(scope_value);
    *err = Err(function_, "Can't nest these things.",
        std::string("You are trying to nest a ") + type_description_ +
        " inside a " + existing->type_description_ + ".");
    err->AppendSubErr(Err(existing->function_, "The enclosing block."));
    return false;
  }

  scope_->SetProperty(&kKey, this);
  key_added_ = true;
  return true;
}

namespace functions {

// assert ----------------------------------------------------------------------

const char kAssert[] = "assert";
const char kAssert_HelpShort[] =
    "assert: Assert an expression is true at generation time.";
const char kAssert_Help[] =
    R"(assert: Assert an expression is true at generation time.

  assert(<condition> [, <error string>])

  If the condition is false, the build will fail with an error. If the
  optional second argument is provided, that string will be printed
  with the error message.

Examples

  assert(is_win)
  assert(defined(sources), "Sources must be defined");
)";

Value RunAssert(Scope* scope,
                const FunctionCallNode* function,
                const std::vector<Value>& args,
                Err* err) {
  if (args.size() != 1 && args.size() != 2) {
    *err = Err(function->function(), "Wrong number of arguments.",
               "assert() takes one or two argument, "
               "were you expecting somethig else?");
  } else if (args[0].type() != Value::BOOLEAN) {
    *err = Err(function->function(), "Assertion value not a bool.");
  } else if (!args[0].boolean_value()) {
    if (args.size() == 2) {
      // Optional string message.
      if (args[1].type() != Value::STRING) {
        *err = Err(function->function(), "Assertion failed.",
            "<<<ERROR MESSAGE IS NOT A STRING>>>");
      } else {
        *err = Err(function->function(), "Assertion failed.",
            args[1].string_value());
      }
    } else {
      *err = Err(function->function(), "Assertion failed.");
    }

    if (args[0].origin()) {
      // If you do "assert(foo)" we'd ideally like to show you where foo was
      // set, and in this case the origin of the args will tell us that.
      // However, if you do "assert(foo && bar)" the source of the value will
      // be the assert like, which isn't so helpful.
      //
      // So we try to see if the args are from the same line or not. This will
      // break if you do "assert(\nfoo && bar)" and we may show the second line
      // as the source, oh well. The way around this is to check to see if the
      // origin node is inside our function call block.
      Location origin_location = args[0].origin()->GetRange().begin();
      if (origin_location.file() != function->function().location().file() ||
          origin_location.line_number() !=
              function->function().location().line_number()) {
        err->AppendSubErr(Err(args[0].origin()->GetRange(), "",
                              "This is where it was set."));
      }
    }
  }
  return Value();
}

// config ----------------------------------------------------------------------

const char kConfig[] = "config";
const char kConfig_HelpShort[] =
    "config: Defines a configuration object.";
const char kConfig_Help[] =
    R"(config: Defines a configuration object.

  Configuration objects can be applied to targets and specify sets of compiler
  flags, includes, defines, etc. They provide a way to conveniently group sets
  of this configuration information.

  A config is referenced by its label just like a target.

  The values in a config are additive only. If you want to remove a flag you
  need to remove the corresponding config that sets it. The final set of flags,
  defines, etc. for a target is generated in this order:

   1. The values specified directly on the target (rather than using a config.
   2. The configs specified in the target's "configs" list, in order.
   3. Public_configs from a breadth-first traversal of the dependency tree in
      the order that the targets appear in "deps".
   4. All dependent configs from a breadth-first traversal of the dependency
      tree in the order that the targets appear in "deps".

Variables valid in a config definition
)"

    CONFIG_VALUES_VARS_HELP

R"(  Nested configs: configs

Variables on a target used to apply configs

  all_dependent_configs, configs, public_configs

Example

  config("myconfig") {
    includes = [ "include/common" ]
    defines = [ "ENABLE_DOOM_MELON" ]
  }

  executable("mything") {
    configs = [ ":myconfig" ]
  }
)";

Value RunConfig(const FunctionCallNode* function,
                const std::vector<Value>& args,
                Scope* scope,
                Err* err) {
  NonNestableBlock non_nestable(scope, function, "config");
  if (!non_nestable.Enter(err))
    return Value();

  if (!EnsureSingleStringArg(function, args, err) ||
      !EnsureNotProcessingImport(function, scope, err))
    return Value();

  Label label(MakeLabelForScope(scope, function, args[0].string_value()));

  if (g_scheduler->verbose_logging())
    g_scheduler->Log("Defining config", label.GetUserVisibleName(true));

  // Create the new config.
  std::unique_ptr<Config> config(new Config(scope->settings(), label));
  config->set_defined_from(function);
  if (!Visibility::FillItemVisibility(config.get(), scope, err))
    return Value();

  // Fill the flags and such.
  const SourceDir& input_dir = scope->GetSourceDir();
  ConfigValuesGenerator gen(&config->own_values(), scope, input_dir, err);
  gen.Run();
  if (err->has_error())
    return Value();

  // Read sub-configs.
  const Value* configs_value = scope->GetValue(variables::kConfigs, true);
  if (configs_value) {
    ExtractListOfUniqueLabels(*configs_value, scope->GetSourceDir(),
                              ToolchainLabelForScope(scope),
                              &config->configs(), err);
  }
  if (err->has_error())
    return Value();

  // Save the generated item.
  Scope::ItemVector* collector = scope->GetItemCollector();
  if (!collector) {
    *err = Err(function, "Can't define a config in this context.");
    return Value();
  }
  collector->push_back(std::move(config));

  return Value();
}

// declare_args ----------------------------------------------------------------

const char kDeclareArgs[] = "declare_args";
const char kDeclareArgs_HelpShort[] =
    "declare_args: Declare build arguments.";
const char kDeclareArgs_Help[] =
    R"(declare_args: Declare build arguments.

  Introduces the given arguments into the current scope. If they are not
  specified on the command line or in a toolchain's arguments, the default
  values given in the declare_args block will be used. However, these defaults
  will not override command-line values.

  See also "gn help buildargs" for an overview.

  The precise behavior of declare args is:

   1. The declare_args() block executes. Any variable defined in the enclosing
      scope is available for reading, but any variable defined earlier in
      the current scope is not (since the overrides haven't been applied yet).

   2. At the end of executing the block, any variables set within that scope
      are saved globally as build arguments, with their current values being
      saved as the "default value" for that argument.

   3. User-defined overrides are applied. Anything set in "gn args" now
      overrides any default values. The resulting set of variables is promoted
      to be readable from the following code in the file.

  This has some ramifications that may not be obvious:

    - You should not perform difficult work inside a declare_args block since
      this only sets a default value that may be discarded. In particular,
      don't use the result of exec_script() to set the default value. If you
      want to have a script-defined default, set some default "undefined" value
      like [], "", or -1, and after the declare_args block, call exec_script if
      the value is unset by the user.

    - Because you cannot read the value of a variable defined in the same
      block, if you need to make the default value of one arg depend
      on the possibly-overridden value of another, write two separate
      declare_args() blocks:

        declare_args() {
          enable_foo = true
        }
        declare_args() {
          # Bar defaults to same user-overridden state as foo.
          enable_bar = enable_foo
        }

Example

  declare_args() {
    enable_teleporter = true
    enable_doom_melon = false
  }

  If you want to override the (default disabled) Doom Melon:
    gn --args="enable_doom_melon=true enable_teleporter=true"
  This also sets the teleporter, but it's already defaulted to on so it will
  have no effect.
)";

Value RunDeclareArgs(Scope* scope,
                     const FunctionCallNode* function,
                     const std::vector<Value>& args,
                     BlockNode* block,
                     Err* err) {
  NonNestableBlock non_nestable(scope, function, "declare_args");
  if (!non_nestable.Enter(err))
    return Value();

  Scope block_scope(scope);
  block_scope.SetProperty(&kInDeclareArgsKey, &block_scope);
  block->Execute(&block_scope, err);
  if (err->has_error())
    return Value();

  // Pass the values from our scope into the Args object for adding to the
  // scope with the proper values (taking into account the defaults given in
  // the block_scope, and arguments passed into the build).
  Scope::KeyValueMap values;
  block_scope.GetCurrentScopeValues(&values);
  scope->settings()->build_settings()->build_args().DeclareArgs(
      values, scope, err);
  return Value();
}

// defined ---------------------------------------------------------------------

const char kDefined[] = "defined";
const char kDefined_HelpShort[] =
    "defined: Returns whether an identifier is defined.";
const char kDefined_Help[] =
    R"(defined: Returns whether an identifier is defined.

  Returns true if the given argument is defined. This is most useful in
  templates to assert that the caller set things up properly.

  You can pass an identifier:
    defined(foo)
  which will return true or false depending on whether foo is defined in the
  current scope.

  You can also check a named scope:
    defined(foo.bar)
  which will return true or false depending on whether bar is defined in the
  named scope foo. It will throw an error if foo is not defined or is not a
  scope.

Example

  template("mytemplate") {
    # To help users call this template properly...
    assert(defined(invoker.sources), "Sources must be defined")

    # If we want to accept an optional "values" argument, we don't
    # want to dereference something that may not be defined.
    if (defined(invoker.values)) {
      values = invoker.values
    } else {
      values = "some default value"
    }
  }
)";

Value RunDefined(Scope* scope,
                 const FunctionCallNode* function,
                 const ListNode* args_list,
                 Err* err) {
  const auto& args_vector = args_list->contents();
  if (args_vector.size() != 1) {
    *err = Err(function, "Wrong number of arguments to defined().",
               "Expecting exactly one.");
    return Value();
  }

  const IdentifierNode* identifier = args_vector[0]->AsIdentifier();
  if (identifier) {
    // Passed an identifier "defined(foo)".
    if (scope->GetValue(identifier->value().value()))
      return Value(function, true);
    return Value(function, false);
  }

  const AccessorNode* accessor = args_vector[0]->AsAccessor();
  if (accessor) {
    // Passed an accessor "defined(foo.bar)".
    if (accessor->member()) {
      // The base of the accessor must be a scope if it's defined.
      const Value* base = scope->GetValue(accessor->base().value());
      if (!base) {
        *err = Err(accessor, "Undefined identifier");
        return Value();
      }
      if (!base->VerifyTypeIs(Value::SCOPE, err))
        return Value();

      // Check the member inside the scope to see if its defined.
      if (base->scope_value()->GetValue(accessor->member()->value().value()))
        return Value(function, true);
      return Value(function, false);
    }
  }

  // Argument is invalid.
  *err = Err(function, "Bad thing passed to defined().",
      "It should be of the form defined(foo) or defined(foo.bar).");
  return Value();
}

// getenv ----------------------------------------------------------------------

const char kGetEnv[] = "getenv";
const char kGetEnv_HelpShort[] =
    "getenv: Get an environment variable.";
const char kGetEnv_Help[] =
    R"(getenv: Get an environment variable.

  value = getenv(env_var_name)

  Returns the value of the given enironment variable. If the value is not
  found, it will try to look up the variable with the "opposite" case (based on
  the case of the first letter of the variable), but is otherwise
  case-sensitive.

  If the environment variable is not found, the empty string will be returned.
  Note: it might be nice to extend this if we had the concept of "none" in the
  language to indicate lookup failure.

Example

  home_dir = getenv("HOME")
)";

Value RunGetEnv(Scope* scope,
                const FunctionCallNode* function,
                const std::vector<Value>& args,
                Err* err) {
  if (!EnsureSingleStringArg(function, args, err))
    return Value();

  std::unique_ptr<base::Environment> env(base::Environment::Create());

  std::string result;
  if (!env->GetVar(args[0].string_value().c_str(), &result))
    return Value(function, "");  // Not found, return empty string.
  return Value(function, result);
}

// import ----------------------------------------------------------------------

const char kImport[] = "import";
const char kImport_HelpShort[] =
    "import: Import a file into the current scope.";
const char kImport_Help[] =
    R"(import: Import a file into the current scope.

  The import command loads the rules and variables resulting from executing the
  given file into the current scope.

  By convention, imported files are named with a .gni extension.

  An import is different than a C++ "include". The imported file is executed in
  a standalone environment from the caller of the import command. The results
  of this execution are cached for other files that import the same .gni file.

  Note that you can not import a BUILD.gn file that's otherwise used in the
  build. Files must either be imported or implicitly loaded as a result of deps
  rules, but not both.

  The imported file's scope will be merged with the scope at the point import
  was called. If there is a conflict (both the current scope and the imported
  file define some variable or rule with the same name but different value), a
  runtime error will be thrown. Therefore, it's good practice to minimize the
  stuff that an imported file defines.

  Variables and templates beginning with an underscore '_' are considered
  private and will not be imported. Imported files can use such variables for
  internal computation without affecting other files.

Examples

  import("//build/rules/idl_compilation_rule.gni")

  # Looks in the current directory.
  import("my_vars.gni")
)";

Value RunImport(Scope* scope,
                const FunctionCallNode* function,
                const std::vector<Value>& args,
                Err* err) {
  if (!EnsureSingleStringArg(function, args, err))
    return Value();

  const SourceDir& input_dir = scope->GetSourceDir();
  SourceFile import_file =
      input_dir.ResolveRelativeFile(args[0], err,
          scope->settings()->build_settings()->root_path_utf8());
  if (!err->has_error()) {
    scope->settings()->import_manager().DoImport(import_file, function,
                                                 scope, err);
  }
  return Value();
}

// not_needed -----------------------------------------------------------------

const char kNotNeeded[] = "not_needed";
const char kNotNeeded_HelpShort[] =
    "not_needed: Mark variables from scope as not needed.";
const char kNotNeeded_Help[] =
    R"(not_needed: Mark variables from scope as not needed.

  not_needed(variable_list_or_star, variable_to_ignore_list = [])
  not_needed(from_scope, variable_list_or_star,
             variable_to_ignore_list = [])

  Mark the variables in the current or given scope as not needed, which means
  you will not get an error about unused variables for these. The
  variable_to_ignore_list allows excluding variables from "all matches" if
  variable_list_or_star is "*".

Example

  not_needed("*", [ "config" ])
  not_needed([ "data_deps", "deps" ])
  not_needed(invoker, "*", [ "config" ])
  not_needed(invoker, [ "data_deps", "deps" ])
)";

Value RunNotNeeded(Scope* scope,
                   const FunctionCallNode* function,
                   const ListNode* args_list,
                   Err* err) {
  const auto& args_vector = args_list->contents();
  if (args_vector.size() < 1 || args_vector.size() > 3) {
    *err = Err(function, "Wrong number of arguments.",
               "Expecting one, two or three arguments.");
    return Value();
  }
  auto args_cur = args_vector.begin();

  Value* value = nullptr;  // Value to use, may point to result_value.
  Value result_value;      // Storage for the "evaluate" case.
  const IdentifierNode* identifier = (*args_cur)->AsIdentifier();
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
    result_value = (*args_cur)->Execute(scope, err);
    if (err->has_error())
      return Value();
    value = &result_value;
  }
  args_cur++;

  // Extract the source scope if different from current one.
  Scope* source = scope;
  if (value->type() == Value::SCOPE) {
    source = value->scope_value();
    result_value = (*args_cur)->Execute(scope, err);
    if (err->has_error())
      return Value();
    value = &result_value;
    args_cur++;
  }

  // Extract the exclusion list if defined.
  Value exclusion_value;
  std::set<std::string> exclusion_set;
  if (args_cur != args_vector.end()) {
    exclusion_value = (*args_cur)->Execute(source, err);
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

  if (value->type() == Value::STRING) {
    if (value->string_value() == "*") {
      source->MarkAllUsed(exclusion_set);
      return Value();
    }
  } else if (value->type() == Value::LIST) {
    if (exclusion_value.type() != Value::NONE) {
      *err = Err(exclusion_value, "Not supported with a variable list.",
                 "Exclusion list can only be used with the string \"*\".");
      return Value();
    }
    for (const Value& cur : value->list_value()) {
      if (!cur.VerifyTypeIs(Value::STRING, err))
        return Value();
      source->MarkUsed(cur.string_value());
    }
    return Value();
  }

  // Not the right type of argument.
  *err = Err(*value, "Not a valid list of variables.",
             "Expecting either the string \"*\" or a list of strings.");
  return Value();
}

// set_sources_assignment_filter -----------------------------------------------

const char kSetSourcesAssignmentFilter[] = "set_sources_assignment_filter";
const char kSetSourcesAssignmentFilter_HelpShort[] =
    "set_sources_assignment_filter: Set a pattern to filter source files.";
const char kSetSourcesAssignmentFilter_Help[] =
    R"(set_sources_assignment_filter: Set a pattern to filter source files.

  The sources assignment filter is a list of patterns that remove files from
  the list implicitly whenever the "sources" variable is assigned to. This will
  do nothing for non-lists.

  This is intended to be used to globally filter out files with
  platform-specific naming schemes when they don't apply, for example you may
  want to filter out all "*_win.cc" files on non-Windows platforms.

  Typically this will be called once in the master build config script to set
  up the filter for the current platform. Subsequent calls will overwrite the
  previous values.

  If you want to bypass the filter and add a file even if it might be filtered
  out, call set_sources_assignment_filter([]) to clear the list of filters.
  This will apply until the current scope exits

How to use patterns

  File patterns are VERY limited regular expressions. They must match the
  entire input string to be counted as a match. In regular expression parlance,
  there is an implicit "^...$" surrounding your input. If you want to match a
  substring, you need to use wildcards at the beginning and end.

  There are only two special tokens understood by the pattern matcher.
  Everything else is a literal.

   - "*" Matches zero or more of any character. It does not depend on the
     preceding character (in regular expression parlance it is equivalent to
     ".*").

   - "\b" Matches a path boundary. This will match the beginning or end of a
     string, or a slash.

Pattern examples

  "*asdf*"
      Matches a string containing "asdf" anywhere.

  "asdf"
      Matches only the exact string "asdf".

  "*.cc"
      Matches strings ending in the literal ".cc".

  "\bwin/*"
      Matches "win/foo" and "foo/win/bar.cc" but not "iwin/foo".

Sources assignment example

  # Filter out all _win files.
  set_sources_assignment_filter([ "*_win.cc", "*_win.h" ])
  sources = [ "a.cc", "b_win.cc" ]
  print(sources)
  # Will print [ "a.cc" ]. b_win one was filtered out.
)";

Value RunSetSourcesAssignmentFilter(Scope* scope,
                                    const FunctionCallNode* function,
                                    const std::vector<Value>& args,
                                    Err* err) {
  if (args.size() != 1) {
    *err = Err(function, "set_sources_assignment_filter takes one argument.");
  } else {
    std::unique_ptr<PatternList> f(new PatternList);
    f->SetFromValue(args[0], err);
    if (!err->has_error())
      scope->set_sources_assignment_filter(std::move(f));
  }
  return Value();
}

// pool ------------------------------------------------------------------------

const char kPool[] = "pool";
const char kPool_HelpShort[] =
    "pool: Defines a pool object.";
const char kPool_Help[] =
    R"*(pool: Defines a pool object.

  Pool objects can be applied to a tool to limit the parallelism of the
  build. This object has a single property "depth" corresponding to
  the number of tasks that may run simultaneously.

  As the file containing the pool definition may be executed in the
  context of more than one toolchain it is recommended to specify an
  explicit toolchain when defining and referencing a pool.

  A pool is referenced by its label just like a target.

Variables

  depth*
  * = required

Example

  if (current_toolchain == default_toolchain) {
    pool("link_pool") {
      depth = 1
    }
  }

  toolchain("toolchain") {
    tool("link") {
      command = "..."
      pool = ":link_pool($default_toolchain)")
    }
  }
)*";

const char kDepth[] = "depth";

Value RunPool(const FunctionCallNode* function,
              const std::vector<Value>& args,
              Scope* scope,
              Err* err) {
  NonNestableBlock non_nestable(scope, function, "pool");
  if (!non_nestable.Enter(err))
    return Value();

  if (!EnsureSingleStringArg(function, args, err) ||
      !EnsureNotProcessingImport(function, scope, err))
    return Value();

  Label label(MakeLabelForScope(scope, function, args[0].string_value()));

  if (g_scheduler->verbose_logging())
    g_scheduler->Log("Defining pool", label.GetUserVisibleName(true));

  // Get the pool depth. It is an error to define a pool without a depth,
  // so check first for the presence of the value.
  const Value* depth = scope->GetValue(kDepth, true);
  if (!depth) {
    *err = Err(function, "Can't define a pool without depth.");
    return Value();
  }

  if (!depth->VerifyTypeIs(Value::INTEGER, err))
    return Value();

  if (depth->int_value() < 0) {
    *err = Err(function, "depth must be positive or nul.");
    return Value();
  }

  // Create the new pool.
  std::unique_ptr<Pool> pool(new Pool(scope->settings(), label));
  pool->set_depth(depth->int_value());

  // Save the generated item.
  Scope::ItemVector* collector = scope->GetItemCollector();
  if (!collector) {
    *err = Err(function, "Can't define a pool in this context.");
    return Value();
  }
  collector->push_back(std::move(pool));

  return Value();
}

// print -----------------------------------------------------------------------

const char kPrint[] = "print";
const char kPrint_HelpShort[] =
    "print: Prints to the console.";
const char kPrint_Help[] =
    R"(print: Prints to the console.

  Prints all arguments to the console separated by spaces. A newline is
  automatically appended to the end.

  This function is intended for debugging. Note that build files are run in
  parallel so you may get interleaved prints. A buildfile may also be executed
  more than once in parallel in the context of different toolchains so the
  prints from one file may be duplicated or
  interleaved with itself.

Examples

  print("Hello world")

  print(sources, deps)
)";

Value RunPrint(Scope* scope,
               const FunctionCallNode* function,
               const std::vector<Value>& args,
               Err* err) {
  std::string output;
  for (size_t i = 0; i < args.size(); i++) {
    if (i != 0)
      output.push_back(' ');
    output.append(args[i].ToString(false));
  }
  output.push_back('\n');

  const BuildSettings::PrintCallback& cb =
      scope->settings()->build_settings()->print_callback();
  if (cb.is_null())
    printf("%s", output.c_str());
  else
    cb.Run(output);

  return Value();
}

// split_list ------------------------------------------------------------------

const char kSplitList[] = "split_list";
const char kSplitList_HelpShort[] =
    "split_list: Splits a list into N different sub-lists.";
const char kSplitList_Help[] =
    R"(split_list: Splits a list into N different sub-lists.

  result = split_list(input, n)

  Given a list and a number N, splits the list into N sub-lists of
  approximately equal size. The return value is a list of the sub-lists. The
  result will always be a list of size N. If N is greater than the number of
  elements in the input, it will be padded with empty lists.

  The expected use is to divide source files into smaller uniform chunks.

Example

  The code:
    mylist = [1, 2, 3, 4, 5, 6]
    print(split_list(mylist, 3))

  Will print:
    [[1, 2], [3, 4], [5, 6]
)";
Value RunSplitList(Scope* scope,
                   const FunctionCallNode* function,
                   const ListNode* args_list,
                   Err* err) {
  const auto& args_vector = args_list->contents();
  if (args_vector.size() != 2) {
    *err = Err(function, "Wrong number of arguments to split_list().",
               "Expecting exactly two.");
    return Value();
  }

  ParseNodeValueAdapter list_adapter;
  if (!list_adapter.InitForType(scope, args_vector[0].get(), Value::LIST, err))
    return Value();
  const std::vector<Value>& input = list_adapter.get().list_value();

  ParseNodeValueAdapter count_adapter;
  if (!count_adapter.InitForType(scope, args_vector[1].get(), Value::INTEGER,
                                 err))
    return Value();
  int64_t count = count_adapter.get().int_value();
  if (count <= 0) {
    *err = Err(function, "Requested result size is not positive.");
    return Value();
  }

  Value result(function, Value::LIST);
  result.list_value().resize(count);

  // Every result list gets at least this many items in it.
  int64_t min_items_per_list = static_cast<int64_t>(input.size()) / count;

  // This many result lists get an extra item which is the remainder from above.
  int64_t extra_items = static_cast<int64_t>(input.size()) % count;

  // Allocate all lists that have a remainder assigned to them (max items).
  int64_t max_items_per_list = min_items_per_list + 1;
  auto last_item_end = input.begin();
  for (int64_t i = 0; i < extra_items; i++) {
    result.list_value()[i] = Value(function, Value::LIST);

    auto begin_add = last_item_end;
    last_item_end += max_items_per_list;
    result.list_value()[i].list_value().assign(begin_add, last_item_end);
  }

  // Allocate all smaller items that don't have a remainder.
  for (int64_t i = extra_items; i < count; i++) {
    result.list_value()[i] = Value(function, Value::LIST);

    auto begin_add = last_item_end;
    last_item_end += min_items_per_list;
    result.list_value()[i].list_value().assign(begin_add, last_item_end);
  }

  return result;
}

// -----------------------------------------------------------------------------

FunctionInfo::FunctionInfo()
    : self_evaluating_args_runner(nullptr),
      generic_block_runner(nullptr),
      executed_block_runner(nullptr),
      no_block_runner(nullptr),
      help_short(nullptr),
      help(nullptr),
      is_target(false) {
}

FunctionInfo::FunctionInfo(SelfEvaluatingArgsFunction seaf,
                           const char* in_help_short,
                           const char* in_help,
                           bool in_is_target)
    : self_evaluating_args_runner(seaf),
      generic_block_runner(nullptr),
      executed_block_runner(nullptr),
      no_block_runner(nullptr),
      help_short(in_help_short),
      help(in_help),
      is_target(in_is_target) {
}

FunctionInfo::FunctionInfo(GenericBlockFunction gbf,
                           const char* in_help_short,
                           const char* in_help,
                           bool in_is_target)
    : self_evaluating_args_runner(nullptr),
      generic_block_runner(gbf),
      executed_block_runner(nullptr),
      no_block_runner(nullptr),
      help_short(in_help_short),
      help(in_help),
      is_target(in_is_target) {
}

FunctionInfo::FunctionInfo(ExecutedBlockFunction ebf,
                           const char* in_help_short,
                           const char* in_help,
                           bool in_is_target)
    : self_evaluating_args_runner(nullptr),
      generic_block_runner(nullptr),
      executed_block_runner(ebf),
      no_block_runner(nullptr),
      help_short(in_help_short),
      help(in_help),
      is_target(in_is_target) {
}

FunctionInfo::FunctionInfo(NoBlockFunction nbf,
                           const char* in_help_short,
                           const char* in_help,
                           bool in_is_target)
    : self_evaluating_args_runner(nullptr),
      generic_block_runner(nullptr),
      executed_block_runner(nullptr),
      no_block_runner(nbf),
      help_short(in_help_short),
      help(in_help),
      is_target(in_is_target) {
}

// Setup the function map via a static initializer. We use this because it
// avoids race conditions without having to do some global setup function or
// locking-heavy singleton checks at runtime. In practice, we always need this
// before we can do anything interesting, so it's OK to wait for the
// initializer.
struct FunctionInfoInitializer {
  FunctionInfoMap map;

  FunctionInfoInitializer() {
    #define INSERT_FUNCTION(command, is_target) \
        map[k##command] = FunctionInfo(&Run##command, \
                                       k##command##_HelpShort, \
                                       k##command##_Help, \
                                       is_target);

    INSERT_FUNCTION(Action, true)
    INSERT_FUNCTION(ActionForEach, true)
    INSERT_FUNCTION(BundleData, true)
    INSERT_FUNCTION(CreateBundle, true)
    INSERT_FUNCTION(Copy, true)
    INSERT_FUNCTION(Executable, true)
    INSERT_FUNCTION(Group, true)
    INSERT_FUNCTION(LoadableModule, true)
    INSERT_FUNCTION(SharedLibrary, true)
    INSERT_FUNCTION(SourceSet, true)
    INSERT_FUNCTION(StaticLibrary, true)
    INSERT_FUNCTION(Target, true)

    INSERT_FUNCTION(Assert, false)
    INSERT_FUNCTION(Config, false)
    INSERT_FUNCTION(DeclareArgs, false)
    INSERT_FUNCTION(Defined, false)
    INSERT_FUNCTION(ExecScript, false)
    INSERT_FUNCTION(ForEach, false)
    INSERT_FUNCTION(ForwardVariablesFrom, false)
    INSERT_FUNCTION(GetEnv, false)
    INSERT_FUNCTION(GetLabelInfo, false)
    INSERT_FUNCTION(GetPathInfo, false)
    INSERT_FUNCTION(GetTargetOutputs, false)
    INSERT_FUNCTION(Import, false)
    INSERT_FUNCTION(NotNeeded, false)
    INSERT_FUNCTION(Pool, false)
    INSERT_FUNCTION(Print, false)
    INSERT_FUNCTION(ProcessFileTemplate, false)
    INSERT_FUNCTION(ReadFile, false)
    INSERT_FUNCTION(RebasePath, false)
    INSERT_FUNCTION(SetDefaults, false)
    INSERT_FUNCTION(SetDefaultToolchain, false)
    INSERT_FUNCTION(SetSourcesAssignmentFilter, false)
    INSERT_FUNCTION(SplitList, false)
    INSERT_FUNCTION(Template, false)
    INSERT_FUNCTION(Tool, false)
    INSERT_FUNCTION(Toolchain, false)
    INSERT_FUNCTION(WriteFile, false)

    #undef INSERT_FUNCTION
  }
};
const FunctionInfoInitializer function_info;

const FunctionInfoMap& GetFunctions() {
  return function_info.map;
}

Value RunFunction(Scope* scope,
                  const FunctionCallNode* function,
                  const ListNode* args_list,
                  BlockNode* block,
                  Err* err) {
  const Token& name = function->function();

  std::string template_name = function->function().value().as_string();
  const Template* templ = scope->GetTemplate(template_name);
  if (templ) {
    Value args = args_list->Execute(scope, err);
    if (err->has_error())
      return Value();
    return templ->Invoke(scope, function, template_name, args.list_value(),
                         block, err);
  }

  // No template matching this, check for a built-in function.
  const FunctionInfoMap& function_map = GetFunctions();
  FunctionInfoMap::const_iterator found_function =
      function_map.find(name.value());
  if (found_function == function_map.end()) {
    *err = Err(name, "Unknown function.");
    return Value();
  }

  if (found_function->second.self_evaluating_args_runner) {
    // Self evaluating args functions are special weird built-ins like foreach.
    // Rather than force them all to check that they have a block or no block
    // and risk bugs for new additions, check a whitelist here.
    if (found_function->second.self_evaluating_args_runner != &RunForEach) {
      if (!VerifyNoBlockForFunctionCall(function, block, err))
        return Value();
    }
    return found_function->second.self_evaluating_args_runner(
        scope, function, args_list, err);
  }

  // All other function types take a pre-executed set of args.
  Value args = args_list->Execute(scope, err);
  if (err->has_error())
    return Value();

  if (found_function->second.generic_block_runner) {
    if (!block) {
      FillNeedsBlockError(function, err);
      return Value();
    }
    return found_function->second.generic_block_runner(
        scope, function, args.list_value(), block, err);
  }

  if (found_function->second.executed_block_runner) {
    if (!block) {
      FillNeedsBlockError(function, err);
      return Value();
    }

    Scope block_scope(scope);
    block->Execute(&block_scope, err);
    if (err->has_error())
      return Value();

    Value result = found_function->second.executed_block_runner(
        function, args.list_value(), &block_scope, err);
    if (err->has_error())
      return Value();

    if (!block_scope.CheckForUnusedVars(err))
      return Value();
    return result;
  }

  // Otherwise it's a no-block function.
  if (!VerifyNoBlockForFunctionCall(function, block, err))
    return Value();
  return found_function->second.no_block_runner(scope, function,
                                                args.list_value(), err);
}

}  // namespace functions
