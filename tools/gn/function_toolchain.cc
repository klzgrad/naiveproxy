// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <limits>
#include <utility>

#include "tools/gn/err.h"
#include "tools/gn/functions.h"
#include "tools/gn/label.h"
#include "tools/gn/label_ptr.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/scope.h"
#include "tools/gn/settings.h"
#include "tools/gn/tool.h"
#include "tools/gn/toolchain.h"
#include "tools/gn/value_extractors.h"
#include "tools/gn/variables.h"

namespace functions {

namespace {

// This is just a unique value to take the address of to use as the key for
// the toolchain property on a scope.
const int kToolchainPropertyKey = 0;

bool ReadBool(Scope* scope,
              const char* var,
              Tool* tool,
              void (Tool::*set)(bool),
              Err* err) {
  const Value* v = scope->GetValue(var, true);
  if (!v)
    return true;  // Not present is fine.
  if (!v->VerifyTypeIs(Value::BOOLEAN, err))
    return false;

  (tool->*set)(v->boolean_value());
  return true;
}

// Reads the given string from the scope (if present) and puts the result into
// dest. If the value is not a string, sets the error and returns false.
bool ReadString(Scope* scope,
                const char* var,
                Tool* tool,
                void (Tool::*set)(std::string),
                Err* err) {
  const Value* v = scope->GetValue(var, true);
  if (!v)
    return true;  // Not present is fine.
  if (!v->VerifyTypeIs(Value::STRING, err))
    return false;

  (tool->*set)(v->string_value());
  return true;
}

// Reads the given label from the scope (if present) and puts the result into
// dest. If the value is not a label, sets the error and returns false.
bool ReadLabel(Scope* scope,
               const char* var,
               Tool* tool,
               const Label& current_toolchain,
               void (Tool::*set)(LabelPtrPair<Pool>),
               Err* err) {
  const Value* v = scope->GetValue(var, true);
  if (!v)
    return true;  // Not present is fine.

  Label label =
      Label::Resolve(scope->GetSourceDir(), current_toolchain, *v, err);
  if (err->has_error())
    return false;

  LabelPtrPair<Pool> pair(label);
  pair.origin = tool->defined_from();

  (tool->*set)(std::move(pair));
  return true;
}

// Calls the given validate function on each type in the list. On failure,
// sets the error, blame the value, and return false.
bool ValidateSubstitutionList(const std::vector<SubstitutionType>& list,
                              bool (*validate)(SubstitutionType),
                              const Value* origin,
                              Err* err) {
  for (const auto& cur_type : list) {
    if (!validate(cur_type)) {
      *err = Err(*origin, "Pattern not valid here.",
          "You used the pattern " + std::string(kSubstitutionNames[cur_type]) +
          " which is not valid\nfor this variable.");
      return false;
    }
  }
  return true;
}

bool ReadPattern(Scope* scope,
                 const char* name,
                 bool (*validate)(SubstitutionType),
                 Tool* tool,
                 void (Tool::*set)(SubstitutionPattern),
                 Err* err) {
  const Value* value = scope->GetValue(name, true);
  if (!value)
    return true;  // Not present is fine.
  if (!value->VerifyTypeIs(Value::STRING, err))
    return false;

  SubstitutionPattern pattern;
  if (!pattern.Parse(*value, err))
    return false;
  if (!ValidateSubstitutionList(pattern.required_types(), validate, value, err))
    return false;

  (tool->*set)(std::move(pattern));
  return true;
}

bool ReadPatternList(Scope* scope,
                     const char* name,
                     bool (*validate)(SubstitutionType),
                     Tool* tool,
                     void (Tool::*set)(SubstitutionList),
                     Err* err) {
  const Value* value = scope->GetValue(name, true);
  if (!value)
    return true;  // Not present is fine.
  if (!value->VerifyTypeIs(Value::LIST, err))
    return false;

  SubstitutionList list;
  if (!list.Parse(*value, err))
    return false;

  // Validate the right kinds of patterns are used.
  if (!ValidateSubstitutionList(list.required_types(), validate, value, err))
    return false;

  (tool->*set)(std::move(list));
  return true;
}

bool ReadOutputExtension(Scope* scope, Tool* tool, Err* err) {
  const Value* value = scope->GetValue("default_output_extension", true);
  if (!value)
    return true;  // Not present is fine.
  if (!value->VerifyTypeIs(Value::STRING, err))
    return false;

  if (value->string_value().empty())
    return true;  // Accept empty string.

  if (value->string_value()[0] != '.') {
    *err = Err(*value, "default_output_extension must begin with a '.'");
    return false;
  }

  tool->set_default_output_extension(value->string_value());
  return true;
}

bool ReadPrecompiledHeaderType(Scope* scope, Tool* tool, Err* err) {
  const Value* value = scope->GetValue("precompiled_header_type", true);
  if (!value)
    return true;  // Not present is fine.
  if (!value->VerifyTypeIs(Value::STRING, err))
    return false;

  if (value->string_value().empty())
    return true;  // Accept empty string, do nothing (default is "no PCH").

  if (value->string_value() == "gcc") {
    tool->set_precompiled_header_type(Tool::PCH_GCC);
    return true;
  } else if (value->string_value() == "msvc") {
    tool->set_precompiled_header_type(Tool::PCH_MSVC);
    return true;
  }
  *err = Err(*value, "Invalid precompiled_header_type",
             "Must either be empty, \"gcc\", or \"msvc\".");
  return false;
}

bool ReadDepsFormat(Scope* scope, Tool* tool, Err* err) {
  const Value* value = scope->GetValue("depsformat", true);
  if (!value)
    return true;  // Not present is fine.
  if (!value->VerifyTypeIs(Value::STRING, err))
    return false;

  if (value->string_value() == "gcc") {
    tool->set_depsformat(Tool::DEPS_GCC);
  } else if (value->string_value() == "msvc") {
    tool->set_depsformat(Tool::DEPS_MSVC);
  } else {
    *err = Err(*value, "Deps format must be \"gcc\" or \"msvc\".");
    return false;
  }
  return true;
}

bool IsCompilerTool(Toolchain::ToolType type) {
  return type == Toolchain::TYPE_CC ||
         type == Toolchain::TYPE_CXX ||
         type == Toolchain::TYPE_OBJC ||
         type == Toolchain::TYPE_OBJCXX ||
         type == Toolchain::TYPE_RC ||
         type == Toolchain::TYPE_ASM;
}

bool IsLinkerTool(Toolchain::ToolType type) {
  // "alink" is not counted as in the generic "linker" tool list.
  return type == Toolchain::TYPE_SOLINK ||
         type == Toolchain::TYPE_SOLINK_MODULE ||
         type == Toolchain::TYPE_LINK;
}

bool IsPatternInOutputList(const SubstitutionList& output_list,
                           const SubstitutionPattern& pattern) {
  for (const auto& cur : output_list.list()) {
    if (pattern.ranges().size() == cur.ranges().size() &&
        std::equal(pattern.ranges().begin(), pattern.ranges().end(),
                   cur.ranges().begin()))
      return true;
  }
  return false;
}


bool ValidateOutputs(const Tool* tool, Err* err) {
  if (tool->outputs().list().empty()) {
    *err = Err(tool->defined_from(),
               "\"outputs\" must be specified for this tool.");
    return false;
  }
  return true;
}

// Validates either link_output or depend_output. To generalize to either, pass
// the associated pattern, and the variable name that should appear in error
// messages.
bool ValidateLinkAndDependOutput(const Tool* tool,
                                 Toolchain::ToolType tool_type,
                                 const SubstitutionPattern& pattern,
                                 const char* variable_name,
                                 Err* err) {
  if (pattern.empty())
    return true;  // Empty is always OK.

  // It should only be specified for certain tool types.
  if (tool_type != Toolchain::TYPE_SOLINK &&
      tool_type != Toolchain::TYPE_SOLINK_MODULE) {
    *err = Err(tool->defined_from(),
        "This tool specifies a " + std::string(variable_name) + ".",
        "This is only valid for solink and solink_module tools.");
    return false;
  }

  if (!IsPatternInOutputList(tool->outputs(), pattern)) {
    *err = Err(tool->defined_from(), "This tool's link_output is bad.",
               "It must match one of the outputs.");
    return false;
  }

  return true;
}

bool ValidateRuntimeOutputs(const Tool* tool,
                            Toolchain::ToolType tool_type,
                            Err* err) {
  if (tool->runtime_outputs().list().empty())
    return true;  // Empty is always OK.

  if (!IsLinkerTool(tool_type)) {
    *err = Err(tool->defined_from(), "This tool specifies runtime_outputs.",
        "This is only valid for linker tools (alink doesn't count).");
    return false;
  }

  for (const SubstitutionPattern& pattern : tool->runtime_outputs().list()) {
    if (!IsPatternInOutputList(tool->outputs(), pattern)) {
      *err = Err(tool->defined_from(), "This tool's runtime_outputs is bad.",
                 "It must be a subset of the outputs. The bad one is:\n  " +
                  pattern.AsString());
      return false;
    }
  }
  return true;
}

}  // namespace

// toolchain -------------------------------------------------------------------

const char kToolchain[] = "toolchain";
const char kToolchain_HelpShort[] =
    "toolchain: Defines a toolchain.";
const char kToolchain_Help[] =
    R"*(toolchain: Defines a toolchain.

  A toolchain is a set of commands and build flags used to compile the source
  code. The toolchain() function defines these commands.

Toolchain overview

  You can have more than one toolchain in use at once in a build and a target
  can exist simultaneously in multiple toolchains. A build file is executed
  once for each toolchain it is referenced in so the GN code can vary all
  parameters of each target (or which targets exist) on a per-toolchain basis.

  When you have a simple build with only one toolchain, the build config file
  is loaded only once at the beginning of the build. It must call
  set_default_toolchain() (see "gn help set_default_toolchain") to tell GN the
  label of the toolchain definition to use. The "toolchain_args" section of the
  toolchain definition is ignored.

  When a target has a dependency on a target using different toolchain (see "gn
  help labels" for how to specify this), GN will start a build using that
  secondary toolchain to resolve the target. GN will load the build config file
  with the build arguements overridden as specified in the toolchain_args.
  Because the default toolchain is already known, calls to
  set_default_toolchain() are ignored.

  To load a file in an alternate toolchain, GN does the following:

    1. Loads the file with the toolchain definition in it (as determined by the
       toolchain label).
    2. Re-runs the master build configuration file, applying the arguments
       specified by the toolchain_args section of the toolchain definition.
    3. Loads the destination build file in the context of the configuration file
       in the previous step.

  The toolchain configuration is two-way. In the default toolchain (i.e. the
  main build target) the configuration flows from the build config file to the
  toolchain. The build config file looks at the state of the build (OS type,
  CPU architecture, etc.) and decides which toolchain to use (via
  set_default_toolchain()). In secondary toolchains, the configuration flows
  from the toolchain to the build config file: the "toolchain_args" in the
  toolchain definition specifies the arguments to re-invoke the build.

Functions and variables

  tool()
    The tool() function call specifies the commands commands to run for a given
    step. See "gn help tool".

  toolchain_args
    Overrides for build arguments to pass to the toolchain when invoking it.
    This is a variable of type "scope" where the variable names correspond to
    variables in declare_args() blocks.

    When you specify a target using an alternate toolchain, the master build
    configuration file is re-interpreted in the context of that toolchain.
    toolchain_args allows you to control the arguments passed into this
    alternate invocation of the build.

    Any default system arguments or arguments passed in via "gn args" will also
    be passed to the alternate invocation unless explicitly overridden by
    toolchain_args.

    The toolchain_args will be ignored when the toolchain being defined is the
    default. In this case, it's expected you want the default argument values.

    See also "gn help buildargs" for an overview of these arguments.

  deps
    Dependencies of this toolchain. These dependencies will be resolved before
    any target in the toolchain is compiled. To avoid circular dependencies
    these must be targets defined in another toolchain.

    This is expressed as a list of targets, and generally these targets will
    always specify a toolchain:
      deps = [ "//foo/bar:baz(//build/toolchain:bootstrap)" ]

    This concept is somewhat inefficient to express in Ninja (it requires a lot
    of duplicate of rules) so should only be used when absolutely necessary.

Example of defining a toolchain

  toolchain("32") {
    tool("cc") {
      command = "gcc {{source}}"
      ...
    }

    toolchain_args = {
      use_doom_melon = true  # Doom melon always required for 32-bit builds.
      current_cpu = "x86"
    }
  }

  toolchain("64") {
    tool("cc") {
      command = "gcc {{source}}"
      ...
    }

    toolchain_args = {
      # use_doom_melon is not overridden here, it will take the default.
      current_cpu = "x64"
    }
  }

Example of cross-toolchain dependencies

  If a 64-bit target wants to depend on a 32-bit binary, it would specify a
  dependency using data_deps (data deps are like deps that are only needed at
  runtime and aren't linked, since you can't link a 32-bit and a 64-bit
  library).

    executable("my_program") {
      ...
      if (target_cpu == "x64") {
        # The 64-bit build needs this 32-bit helper.
        data_deps = [ ":helper(//toolchains:32)" ]
      }
    }

    if (target_cpu == "x86") {
      # Our helper library is only compiled in 32-bits.
      shared_library("helper") {
        ...
      }
    }
)*";

Value RunToolchain(Scope* scope,
                   const FunctionCallNode* function,
                   const std::vector<Value>& args,
                   BlockNode* block,
                   Err* err) {
  NonNestableBlock non_nestable(scope, function, "toolchain");
  if (!non_nestable.Enter(err))
    return Value();

  if (!EnsureNotProcessingImport(function, scope, err) ||
      !EnsureNotProcessingBuildConfig(function, scope, err))
    return Value();

  // Note that we don't want to use MakeLabelForScope since that will include
  // the toolchain name in the label, and toolchain labels don't themselves
  // have toolchain names.
  const SourceDir& input_dir = scope->GetSourceDir();
  Label label(input_dir, args[0].string_value());
  if (g_scheduler->verbose_logging())
    g_scheduler->Log("Defining toolchain", label.GetUserVisibleName(false));

  // This object will actually be copied into the one owned by the toolchain
  // manager, but that has to be done in the lock.
  std::unique_ptr<Toolchain> toolchain(new Toolchain(scope->settings(), label));
  toolchain->set_defined_from(function);
  toolchain->visibility().SetPublic();

  Scope block_scope(scope);
  block_scope.SetProperty(&kToolchainPropertyKey, toolchain.get());
  block->Execute(&block_scope, err);
  block_scope.SetProperty(&kToolchainPropertyKey, nullptr);
  if (err->has_error())
    return Value();

  // Read deps (if any).
  const Value* deps_value = block_scope.GetValue(variables::kDeps, true);
  if (deps_value) {
    ExtractListOfLabels(
        *deps_value, block_scope.GetSourceDir(),
        ToolchainLabelForScope(&block_scope), &toolchain->deps(), err);
    if (err->has_error())
      return Value();
  }

  // Read toolchain args (if any).
  const Value* toolchain_args = block_scope.GetValue("toolchain_args", true);
  if (toolchain_args) {
    if (!toolchain_args->VerifyTypeIs(Value::SCOPE, err))
      return Value();

    Scope::KeyValueMap values;
    toolchain_args->scope_value()->GetCurrentScopeValues(&values);
    toolchain->args() = values;
  }

  if (!block_scope.CheckForUnusedVars(err))
    return Value();

  // Save this toolchain.
  toolchain->ToolchainSetupComplete();
  Scope::ItemVector* collector = scope->GetItemCollector();
  if (!collector) {
    *err = Err(function, "Can't define a toolchain in this context.");
    return Value();
  }
  collector->push_back(std::move(toolchain));
  return Value();
}

// tool ------------------------------------------------------------------------

const char kTool[] = "tool";
const char kTool_HelpShort[] =
    "tool: Specify arguments to a toolchain tool.";
const char kTool_Help[] =
    R"(tool: Specify arguments to a toolchain tool.

Usage

  tool(<tool type>) {
    <tool variables...>
  }

Tool types

    Compiler tools:
      "cc": C compiler
      "cxx": C++ compiler
      "objc": Objective C compiler
      "objcxx": Objective C++ compiler
      "rc": Resource compiler (Windows .rc files)
      "asm": Assembler

    Linker tools:
      "alink": Linker for static libraries (archives)
      "solink": Linker for shared libraries
      "link": Linker for executables

    Other tools:
      "stamp": Tool for creating stamp files
      "copy": Tool to copy files.
      "action": Defaults for actions

    Platform specific tools:
      "copy_bundle_data": [iOS, macOS] Tool to copy files in a bundle.
      "compile_xcassets": [iOS, macOS] Tool to compile asset catalogs.

Tool variables

    command  [string with substitutions]
        Valid for: all tools except "action" (required)

        The command to run.

    default_output_dir  [string with substitutions]
        Valid for: linker tools

        Default directory name for the output file relative to the
        root_build_dir. It can contain other substitution patterns. This will
        be the default value for the {{output_dir}} expansion (discussed below)
        but will be overridden by the "output_dir" variable in a target, if one
        is specified.

        GN doesn't do anything with this string other than pass it along,
        potentially with target-specific overrides. It is the tool's job to use
        the expansion so that the files will be in the right place.

    default_output_extension  [string]
        Valid for: linker tools

        Extension for the main output of a linkable tool. It includes the
        leading dot. This will be the default value for the
        {{output_extension}} expansion (discussed below) but will be overridden
        by by the "output extension" variable in a target, if one is specified.
        Empty string means no extension.

        GN doesn't actually do anything with this extension other than pass it
        along, potentially with target-specific overrides. One would typically
        use the {{output_extension}} value in the "outputs" to read this value.

        Example: default_output_extension = ".exe"

    depfile  [string with substitutions]
        Valid for: compiler tools (optional)

        If the tool can write ".d" files, this specifies the name of the
        resulting file. These files are used to list header file dependencies
        (or other implicit input dependencies) that are discovered at build
        time. See also "depsformat".

        Example: depfile = "{{output}}.d"

    depsformat  [string]
        Valid for: compiler tools (when depfile is specified)

        Format for the deps outputs. This is either "gcc" or "msvc". See the
        ninja documentation for "deps" for more information.

        Example: depsformat = "gcc"

    description  [string with substitutions, optional]
        Valid for: all tools

        What to print when the command is run.

        Example: description = "Compiling {{source}}"

    lib_switch  [string, optional, link tools only]
    lib_dir_switch  [string, optional, link tools only]
        Valid for: Linker tools except "alink"

        These strings will be prepended to the libraries and library search
        directories, respectively, because linkers differ on how specify them.
        If you specified:
          lib_switch = "-l"
          lib_dir_switch = "-L"
        then the "{{libs}}" expansion for [ "freetype", "expat"] would be
        "-lfreetype -lexpat".

    outputs  [list of strings with substitutions]
        Valid for: Linker and compiler tools (required)

        An array of names for the output files the tool produces. These are
        relative to the build output directory. There must always be at least
        one output file. There can be more than one output (a linker might
        produce a library and an import library, for example).

        This array just declares to GN what files the tool will produce. It is
        your responsibility to specify the tool command that actually produces
        these files.

        If you specify more than one output for shared library links, you
        should consider setting link_output, depend_output, and
        runtime_outputs.

        Example for a compiler tool that produces .obj files:
          outputs = [
            "{{source_out_dir}}/{{source_name_part}}.obj"
          ]

        Example for a linker tool that produces a .dll and a .lib. The use of
        {{target_output_name}}, {{output_extension}} and {{output_dir}} allows
        the target to override these values.
          outputs = [
            "{{output_dir}}/{{target_output_name}}"
                "{{output_extension}}",
            "{{output_dir}}/{{target_output_name}}.lib",
          ]

    pool [label, optional]
        Valid for: all tools (optional)

        Label of the pool to use for the tool. Pools are used to limit the
        number of tasks that can execute concurrently during the build.

        See also "gn help pool".

    link_output  [string with substitutions]
    depend_output  [string with substitutions]
        Valid for: "solink" only (optional)

        These two files specify which of the outputs from the solink tool
        should be used for linking and dependency tracking. These should match
        entries in the "outputs". If unspecified, the first item in the
        "outputs" array will be used for all. See "Separate linking and
        dependencies for shared libraries" below for more.

        On Windows, where the tools produce a .dll shared library and a .lib
        import library, you will want the first two to be the import library
        and the third one to be the .dll file. On Linux, if you're not doing
        the separate linking/dependency optimization, all of these should be
        the .so output.

    output_prefix  [string]
        Valid for: Linker tools (optional)

        Prefix to use for the output name. Defaults to empty. This prefix will
        be prepended to the name of the target (or the output_name if one is
        manually specified for it) if the prefix is not already there. The
        result will show up in the {{output_name}} substitution pattern.

        Individual targets can opt-out of the output prefix by setting:
          output_prefix_override = true
        (see "gn help output_prefix_override").

        This is typically used to prepend "lib" to libraries on
        Posix systems:
          output_prefix = "lib"

    precompiled_header_type  [string]
        Valid for: "cc", "cxx", "objc", "objcxx"

        Type of precompiled headers. If undefined or the empty string,
        precompiled headers will not be used for this tool. Otherwise use "gcc"
        or "msvc".

        For precompiled headers to be used for a given target, the target (or a
        config applied to it) must also specify a "precompiled_header" and, for
        "msvc"-style headers, a "precompiled_source" value. If the type is
        "gcc", then both "precompiled_header" and "precompiled_source" must
        resolve to the same file, despite the different formats required for
        each."

        See "gn help precompiled_header" for more.

    restat  [boolean]
        Valid for: all tools (optional, defaults to false)

        Requests that Ninja check the file timestamp after this tool has run to
        determine if anything changed. Set this if your tool has the ability to
        skip writing output if the output file has not changed.

        Normally, Ninja will assume that when a tool runs the output be new and
        downstream dependents must be rebuild. When this is set to trye, Ninja
        can skip rebuilding downstream dependents for input changes that don't
        actually affect the output.

        Example:
          restat = true

    rspfile  [string with substitutions]
        Valid for: all tools except "action" (optional)

        Name of the response file. If empty, no response file will be
        used. See "rspfile_content".

    rspfile_content  [string with substitutions]
        Valid for: all tools except "action" (required when "rspfile" is used)

        The contents to be written to the response file. This may include all
        or part of the command to send to the tool which allows you to get
        around OS command-line length limits.

        This example adds the inputs and libraries to a response file, but
        passes the linker flags directly on the command line:
          tool("link") {
            command = "link -o {{output}} {{ldflags}} @{{output}}.rsp"
            rspfile = "{{output}}.rsp"
            rspfile_content = "{{inputs}} {{solibs}} {{libs}}"
          }

    runtime_outputs  [string list with substitutions]
        Valid for: linker tools

        If specified, this list is the subset of the outputs that should be
        added to runtime deps (see "gn help runtime_deps"). By default (if
        runtime_outputs is empty or unspecified), it will be the link_output.

Expansions for tool variables

  All paths are relative to the root build directory, which is the current
  directory for running all tools. These expansions are available to all tools:

    {{label}}
        The label of the current target. This is typically used in the
        "description" field for link tools. The toolchain will be omitted from
        the label for targets in the default toolchain, and will be included
        for targets in other toolchains.

    {{label_name}}
        The short name of the label of the target. This is the part after the
        colon. For "//foo/bar:baz" this will be "baz". Unlike
        {{target_output_name}}, this is not affected by the "output_prefix" in
        the tool or the "output_name" set on the target.

    {{output}}
        The relative path and name of the output(s) of the current build step.
        If there is more than one output, this will expand to a list of all of
        them. Example: "out/base/my_file.o"

    {{target_gen_dir}}
    {{target_out_dir}}
        The directory of the generated file and output directories,
        respectively, for the current target. There is no trailing slash. See
        also {{output_dir}} for linker tools. Example: "out/base/test"

    {{target_output_name}}
        The short name of the current target with no path information, or the
        value of the "output_name" variable if one is specified in the target.
        This will include the "output_prefix" if any. See also {{label_name}}.

        Example: "libfoo" for the target named "foo" and an output prefix for
        the linker tool of "lib".

)"  // String break to prevent overflowing the 16K max VC string length.
R"(  Compiler tools have the notion of a single input and a single output, along
  with a set of compiler-specific flags. The following expansions are
  available:

    {{asmflags}}
    {{cflags}}
    {{cflags_c}}
    {{cflags_cc}}
    {{cflags_objc}}
    {{cflags_objcc}}
    {{defines}}
    {{include_dirs}}
        Strings correspond that to the processed flags/defines/include
        directories specified for the target.
        Example: "--enable-foo --enable-bar"

        Defines will be prefixed by "-D" and include directories will be
        prefixed by "-I" (these work with Posix tools as well as Microsoft
        ones).

    {{source}}
        The relative path and name of the current input file.
        Example: "../../base/my_file.cc"

    {{source_file_part}}
        The file part of the source including the extension (with no directory
        information).
        Example: "foo.cc"

    {{source_name_part}}
        The filename part of the source file with no directory or extension.
        Example: "foo"

    {{source_gen_dir}}
    {{source_out_dir}}
        The directory in the generated file and output directories,
        respectively, for the current input file. If the source file is in the
        same directory as the target is declared in, they will will be the same
        as the "target" versions above. Example: "gen/base/test"

  Linker tools have multiple inputs and (potentially) multiple outputs The
  static library tool ("alink") is not considered a linker tool. The following
  expansions are available:

    {{inputs}}
    {{inputs_newline}}
        Expands to the inputs to the link step. This will be a list of object
        files and static libraries.
        Example: "obj/foo.o obj/bar.o obj/somelibrary.a"

        The "_newline" version will separate the input files with newlines
        instead of spaces. This is useful in response files: some linkers can
        take a "-filelist" flag which expects newline separated files, and some
        Microsoft tools have a fixed-sized buffer for parsing each line of a
        response file.

    {{ldflags}}
        Expands to the processed set of ldflags and library search paths
        specified for the target.
        Example: "-m64 -fPIC -pthread -L/usr/local/mylib"

    {{libs}}
        Expands to the list of system libraries to link to. Each will be
        prefixed by the "lib_prefix".

        As a special case to support Mac, libraries with names ending in
        ".framework" will be added to the {{libs}} with "-framework" preceeding
        it, and the lib prefix will be ignored.

        Example: "-lfoo -lbar"

    {{output_dir}}
        The value of the "output_dir" variable in the target, or the the value
        of the "default_output_dir" value in the tool if the target does not
        override the output directory. This will be relative to the
        root_build_dir and will not end in a slash. Will be "." for output to
        the root_build_dir.

        This is subtly different than {{target_out_dir}} which is defined by GN
        based on the target's path and not overridable. {{output_dir}} is for
        the final output, {{target_out_dir}} is generally for object files and
        other outputs.

        Usually {{output_dir}} would be defined in terms of either
        {{target_out_dir}} or {{root_out_dir}}

    {{output_extension}}
        The value of the "output_extension" variable in the target, or the
        value of the "default_output_extension" value in the tool if the target
        does not specify an output extension.
        Example: ".so"

    {{solibs}}
        Extra libraries from shared library dependencide not specified in the
        {{inputs}}. This is the list of link_output files from shared libraries
        (if the solink tool specifies a "link_output" variable separate from
        the "depend_output").

        These should generally be treated the same as libs by your tool.

        Example: "libfoo.so libbar.so"

)"  // String break to prevent overflowing the 16K max VC string length.
R"(  The static library ("alink") tool allows {{arflags}} plus the common tool
  substitutions.

  The copy tool allows the common compiler/linker substitutions, plus
  {{source}} which is the source of the copy. The stamp tool allows only the
  common tool substitutions.

  The copy_bundle_data and compile_xcassets tools only allows the common tool
  substitutions. Both tools are required to create iOS/macOS bundles and need
  only be defined on those platforms.

  The copy_bundle_data tool will be called with one source and needs to copy
  (optionally optimizing the data representation) to its output. It may be
  called with a directory as input and it needs to be recursively copied.

  The compile_xcassets tool will be called with one or more source (each an
  asset catalog) that needs to be compiled to a single output. The following
  substitutions are avaiable:

    {{inputs}}
        Expands to the list of .xcassets to use as input to compile the asset
        catalog.

    {{bundle_product_type}}
        Expands to the product_type of the bundle that will contain the
        compiled asset catalog. Usually corresponds to the product_type
        property of the corresponding create_bundle target.

    {{bundle_partial_info_plist}}
        Expands to the path to the partial Info.plist generated by the
        assets catalog compiler. Usually based on the target_name of
        the create_bundle target.

Separate linking and dependencies for shared libraries

  Shared libraries are special in that not all changes to them require that
  dependent targets be re-linked. If the shared library is changed but no
  imports or exports are different, dependent code needn't be relinked, which
  can speed up the build.

  If your link step can output a list of exports from a shared library and
  writes the file only if the new one is different, the timestamp of this file
  can be used for triggering re-links, while the actual shared library would be
  used for linking.

  You will need to specify
    restat = true
  in the linker tool to make this work, so Ninja will detect if the timestamp
  of the dependency file has changed after linking (otherwise it will always
  assume that running a command updates the output):

    tool("solink") {
      command = "..."
      outputs = [
        "{{output_dir}}/{{target_output_name}}{{output_extension}}",
        "{{output_dir}}/{{target_output_name}}"
            "{{output_extension}}.TOC",
      ]
      link_output =
        "{{output_dir}}/{{target_output_name}}{{output_extension}}"
      depend_output =
        "{{output_dir}}/{{target_output_name}}"
            "{{output_extension}}.TOC"
      restat = true
    }

Example

  toolchain("my_toolchain") {
    # Put these at the top to apply to all tools below.
    lib_prefix = "-l"
    lib_dir_prefix = "-L"

    tool("cc") {
      command = "gcc {{source}} -o {{output}}"
      outputs = [ "{{source_out_dir}}/{{source_name_part}}.o" ]
      description = "GCC {{source}}"
    }
    tool("cxx") {
      command = "g++ {{source}} -o {{output}}"
      outputs = [ "{{source_out_dir}}/{{source_name_part}}.o" ]
      description = "G++ {{source}}"
    }
  };
)";

Value RunTool(Scope* scope,
              const FunctionCallNode* function,
              const std::vector<Value>& args,
              BlockNode* block,
              Err* err) {
  // Find the toolchain definition we're executing inside of. The toolchain
  // function will set a property pointing to it that we'll pick up.
  Toolchain* toolchain = reinterpret_cast<Toolchain*>(
      scope->GetProperty(&kToolchainPropertyKey, nullptr));
  if (!toolchain) {
    *err = Err(function->function(), "tool() called outside of toolchain().",
        "The tool() function can only be used inside a toolchain() "
        "definition.");
    return Value();
  }

  if (!EnsureSingleStringArg(function, args, err))
    return Value();
  const std::string& tool_name = args[0].string_value();
  Toolchain::ToolType tool_type = Toolchain::ToolNameToType(tool_name);
  if (tool_type == Toolchain::TYPE_NONE) {
    *err = Err(args[0], "Unknown tool type");
    return Value();
  }

  // Run the tool block.
  Scope block_scope(scope);
  block->Execute(&block_scope, err);
  if (err->has_error())
    return Value();

  // Figure out which validator to use for the substitution pattern for this
  // tool type. There are different validators for the "outputs" than for the
  // rest of the strings.
  bool (*subst_validator)(SubstitutionType) = nullptr;
  bool (*subst_output_validator)(SubstitutionType) = nullptr;
  if (IsCompilerTool(tool_type)) {
    subst_validator = &IsValidCompilerSubstitution;
    subst_output_validator = &IsValidCompilerOutputsSubstitution;
  } else if (IsLinkerTool(tool_type)) {
    subst_validator = &IsValidLinkerSubstitution;
    subst_output_validator = &IsValidLinkerOutputsSubstitution;
  } else if (tool_type == Toolchain::TYPE_ALINK) {
    subst_validator = &IsValidALinkSubstitution;
    // ALink uses the standard output file patterns as other linker tools.
    subst_output_validator = &IsValidLinkerOutputsSubstitution;
  } else if (tool_type == Toolchain::TYPE_COPY ||
             tool_type == Toolchain::TYPE_COPY_BUNDLE_DATA) {
    subst_validator = &IsValidCopySubstitution;
    subst_output_validator = &IsValidCopySubstitution;
  } else if (tool_type == Toolchain::TYPE_COMPILE_XCASSETS) {
    subst_validator = &IsValidCompileXCassetsSubstitution;
    subst_output_validator = &IsValidCompileXCassetsSubstitution;
  } else {
    subst_validator = &IsValidToolSubstitution;
    subst_output_validator = &IsValidToolSubstitution;
  }

  std::unique_ptr<Tool> tool(new Tool);
  tool->set_defined_from(function);

  if (!ReadPattern(&block_scope, "command", subst_validator, tool.get(),
                   &Tool::set_command, err) ||
      !ReadOutputExtension(&block_scope, tool.get(), err) ||
      !ReadPattern(&block_scope, "depfile", subst_validator, tool.get(),
                   &Tool::set_depfile, err) ||
      !ReadDepsFormat(&block_scope, tool.get(), err) ||
      !ReadPattern(&block_scope, "description", subst_validator, tool.get(),
                   &Tool::set_description, err) ||
      !ReadString(&block_scope, "lib_switch", tool.get(), &Tool::set_lib_switch,
                  err) ||
      !ReadString(&block_scope, "lib_dir_switch", tool.get(),
                  &Tool::set_lib_dir_switch, err) ||
      !ReadPattern(&block_scope, "link_output", subst_validator, tool.get(),
                   &Tool::set_link_output, err) ||
      !ReadPattern(&block_scope, "depend_output", subst_validator, tool.get(),
                   &Tool::set_depend_output, err) ||
      !ReadPatternList(&block_scope, "runtime_outputs", subst_validator,
                       tool.get(), &Tool::set_runtime_outputs, err) ||
      !ReadString(&block_scope, "output_prefix", tool.get(),
                  &Tool::set_output_prefix, err) ||
      !ReadPattern(&block_scope, "default_output_dir", subst_validator,
                   tool.get(), &Tool::set_default_output_dir, err) ||
      !ReadPrecompiledHeaderType(&block_scope, tool.get(), err) ||
      !ReadBool(&block_scope, "restat", tool.get(), &Tool::set_restat, err) ||
      !ReadPattern(&block_scope, "rspfile", subst_validator, tool.get(),
                   &Tool::set_rspfile, err) ||
      !ReadPattern(&block_scope, "rspfile_content", subst_validator, tool.get(),
                   &Tool::set_rspfile_content, err) ||
      !ReadLabel(&block_scope, "pool", tool.get(), toolchain->label(),
                 &Tool::set_pool, err)) {
    return Value();
  }

  if (tool_type != Toolchain::TYPE_COPY && tool_type != Toolchain::TYPE_STAMP &&
      tool_type != Toolchain::TYPE_COPY_BUNDLE_DATA &&
      tool_type != Toolchain::TYPE_COMPILE_XCASSETS &&
      tool_type != Toolchain::TYPE_ACTION) {
    // All tools should have outputs, except the copy, stamp, copy_bundle_data
    // compile_xcassets and action tools that generate their outputs internally.
    if (!ReadPatternList(&block_scope, "outputs", subst_output_validator,
                         tool.get(), &Tool::set_outputs, err) ||
        !ValidateOutputs(tool.get(), err))
      return Value();
  }
  if (!ValidateRuntimeOutputs(tool.get(), tool_type, err))
    return Value();

  // Validate link_output and depend_output.
  if (!ValidateLinkAndDependOutput(tool.get(), tool_type, tool->link_output(),
                                   "link_output", err))
    return Value();
  if (!ValidateLinkAndDependOutput(tool.get(), tool_type, tool->depend_output(),
                                   "depend_output", err))
    return Value();
  if ((!tool->link_output().empty() && tool->depend_output().empty()) ||
      (tool->link_output().empty() && !tool->depend_output().empty())) {
    *err = Err(function, "Both link_output and depend_output should either "
        "be specified or they should both be empty.");
    return Value();
  }

  // Make sure there weren't any vars set in this tool that were unused.
  if (!block_scope.CheckForUnusedVars(err))
    return Value();

  toolchain->SetTool(tool_type, std::move(tool));
  return Value();
}

}  // namespace functions
