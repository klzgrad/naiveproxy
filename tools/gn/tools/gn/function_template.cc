// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/functions.h"

#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"
#include "tools/gn/template.h"
#include "tools/gn/value.h"

namespace functions {

const char kTemplate[] = "template";
const char kTemplate_HelpShort[] = "template: Define a template rule.";
const char kTemplate_Help[] =
    R"(template: Define a template rule.

  A template defines a custom name that acts like a function. It provides a way
  to add to the built-in target types.

  The template() function is used to declare a template. To invoke the
  template, just use the name of the template like any other target type.

  Often you will want to declare your template in a special file that other
  files will import (see "gn help import") so your template rule can be shared
  across build files.

Variables and templates:

  When you call template() it creates a closure around all variables currently
  in scope with the code in the template block. When the template is invoked,
  the closure will be executed.

  When the template is invoked, the code in the caller is executed and passed
  to the template code as an implicit "invoker" variable. The template uses
  this to read state out of the invoking code.

  One thing explicitly excluded from the closure is the "current directory"
  against which relative file names are resolved. The current directory will be
  that of the invoking code, since typically that code specifies the file
  names. This means all files internal to the template should use absolute
  names.

  A template will typically forward some or all variables from the invoking
  scope to a target that it defines. Often, such variables might be optional.
  Use the pattern:

    if (defined(invoker.deps)) {
      deps = invoker.deps
    }

  The function forward_variables_from() provides a shortcut to forward one or
  more or possibly all variables in this manner:

    forward_variables_from(invoker, ["deps", "public_deps"])

Target naming

  Your template should almost always define a built-in target with the name the
  template invoker specified. For example, if you have an IDL template and
  somebody does:
    idl("foo") {...
  you will normally want this to expand to something defining a source_set or
  static_library named "foo" (among other things you may need). This way, when
  another target specifies a dependency on "foo", the static_library or
  source_set will be linked.

  It is also important that any other targets your template expands to have
  unique names, or you will get collisions.

  Access the invoking name in your template via the implicit "target_name"
  variable. This should also be the basis for how other targets that a template
  expands to ensure uniqueness.

  A typical example would be a template that defines an action to generate some
  source files, and a source_set to compile that source. Your template would
  name the source_set "target_name" because that's what you want external
  targets to depend on to link your code. And you would name the action
  something like "${target_name}_action" to make it unique. The source set
  would have a dependency on the action to make it run.

Overriding builtin targets

  You can use template to redefine a built-in target in which case your template
  takes a precedence over the built-in one. All uses of the target from within
  the template definition will refer to the built-in target which makes it
  possible to extend the behavior of the built-in target:

    template("shared_library") {
      shared_library(shlib) {
        forward_variables_from(invoker, "*")
        ...
      }
    }

Example of defining a template

  template("my_idl") {
    # Be nice and help callers debug problems by checking that the variables
    # the template requires are defined. This gives a nice message rather than
    # giving the user an error about an undefined variable in the file defining
    # the template
    #
    # You can also use defined() to give default values to variables
    # unspecified by the invoker.
    assert(defined(invoker.sources),
           "Need sources in $target_name listing the idl files.")

    # Name of the intermediate target that does the code gen. This must
    # incorporate the target name so it's unique across template
    # instantiations.
    code_gen_target_name = target_name + "_code_gen"

    # Intermediate target to convert IDL to C source. Note that the name is
    # based on the name the invoker of the template specified. This way, each
    # time the template is invoked we get a unique intermediate action name
    # (since all target names are in the global scope).
    action_foreach(code_gen_target_name) {
      # Access the scope defined by the invoker via the implicit "invoker"
      # variable.
      sources = invoker.sources

      # Note that we need an absolute path for our script file name. The
      # current directory when executing this code will be that of the invoker
      # (this is why we can use the "sources" directly above without having to
      # rebase all of the paths). But if we need to reference a script relative
      # to the template file, we'll need to use an absolute path instead.
      script = "//tools/idl/idl_code_generator.py"

      # Tell GN how to expand output names given the sources.
      # See "gn help source_expansion" for more.
      outputs = [ "$target_gen_dir/{{source_name_part}}.cc",
                  "$target_gen_dir/{{source_name_part}}.h" ]
    }

    # Name the source set the same as the template invocation so instancing
    # this template produces something that other targets can link to in their
    # deps.
    source_set(target_name) {
      # Generates the list of sources, we get these from the action_foreach
      # above.
      sources = get_target_outputs(":$code_gen_target_name")

      # This target depends on the files produced by the above code gen target.
      deps = [ ":$code_gen_target_name" ]
    }
  }

Example of invoking the resulting template

  # This calls the template code above, defining target_name to be
  # "foo_idl_files" and "invoker" to be the set of stuff defined in the curly
  # brackets.
  my_idl("foo_idl_files") {
    # Goes into the template as "invoker.sources".
    sources = [ "foo.idl", "bar.idl" ]
  }

  # Here is a target that depends on our template.
  executable("my_exe") {
    # Depend on the name we gave the template call above. Internally, this will
    # produce a dependency from executable to the source_set inside the
    # template (since it has this name), which will in turn depend on the code
    # gen action.
    deps = [ ":foo_idl_files" ]
  }
)";

Value RunTemplate(Scope* scope,
                  const FunctionCallNode* function,
                  const std::vector<Value>& args,
                  BlockNode* block,
                  Err* err) {
  // Of course you can have configs and targets in a template. But here, we're
  // not actually executing the block, only declaring it. Marking the template
  // declaration as non-nestable means that you can't put it inside a target,
  // for example.
  NonNestableBlock non_nestable(scope, function, "template");
  if (!non_nestable.Enter(err))
    return Value();

  // TODO(brettw) determine if the function is built-in and throw an error if
  // it is.
  if (args.size() != 1) {
    *err =
        Err(function->function(), "Need exactly one string arg to template.");
    return Value();
  }
  if (!args[0].VerifyTypeIs(Value::STRING, err))
    return Value();
  std::string template_name = args[0].string_value();

  const Template* existing_template = scope->GetTemplate(template_name);
  if (existing_template) {
    *err = Err(function, "Duplicate template definition.",
               "A template with this name was already defined.");
    err->AppendSubErr(
        Err(existing_template->GetDefinitionRange(), "Previous definition."));
    return Value();
  }

  scope->AddTemplate(template_name, new Template(scope, function));

  // The template object above created a closure around the variables in the
  // current scope. The template code will execute in that context when it's
  // invoked. But this means that any variables defined above that are used
  // by the template won't get marked used just by defining the template. The
  // result can be spurious unused variable errors.
  //
  // The "right" thing to do would be to walk the syntax tree inside the
  // template, find all identifier references, and mark those variables used.
  // This is annoying and error-prone to implement and takes extra time to run
  // for this narrow use case.
  //
  // Templates are most often defined in .gni files which don't get
  // used-variable checking anyway, and this case is annoying enough that the
  // incremental value of unused variable checking isn't worth the
  // alternatives. So all values in scope before this template definition are
  // exempted from unused variable checking.
  scope->MarkAllUsed();

  return Value();
}

}  // namespace functions
