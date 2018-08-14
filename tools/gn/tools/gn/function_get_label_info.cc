// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/functions.h"
#include "tools/gn/label.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/value.h"

namespace functions {

const char kGetLabelInfo[] = "get_label_info";
const char kGetLabelInfo_HelpShort[] =
    "get_label_info: Get an attribute from a target's label.";
const char kGetLabelInfo_Help[] =
    R"*(get_label_info: Get an attribute from a target's label.

  get_label_info(target_label, what)

  Given the label of a target, returns some attribute of that target. The
  target need not have been previously defined in the same file, since none of
  the attributes depend on the actual target definition, only the label itself.

  See also "gn help get_target_outputs".

Possible values for the "what" parameter

  "name"
      The short name of the target. This will match the value of the
      "target_name" variable inside that target's declaration. For the label
      "//foo/bar:baz" this will return "baz".

  "dir"
      The directory containing the target's definition, with no slash at the
      end. For the label "//foo/bar:baz" this will return "//foo/bar".

  "target_gen_dir"
      The generated file directory for the target. This will match the value of
      the "target_gen_dir" variable when inside that target's declaration.

  "root_gen_dir"
      The root of the generated file tree for the target. This will match the
      value of the "root_gen_dir" variable when inside that target's
      declaration.

  "target_out_dir
      The output directory for the target. This will match the value of the
      "target_out_dir" variable when inside that target's declaration.

  "root_out_dir"
      The root of the output file tree for the target. This will match the
      value of the "root_out_dir" variable when inside that target's
      declaration.

  "label_no_toolchain"
      The fully qualified version of this label, not including the toolchain.
      For the input ":bar" it might return "//foo:bar".

  "label_with_toolchain"
      The fully qualified version of this label, including the toolchain. For
      the input ":bar" it might return "//foo:bar(//toolchain:x64)".

  "toolchain"
      The label of the toolchain. This will match the value of the
      "current_toolchain" variable when inside that target's declaration.

Examples

  get_label_info(":foo", "name")
  # Returns string "foo".

  get_label_info("//foo/bar:baz", "target_gen_dir")
  # Returns string "//out/Debug/gen/foo/bar".
)*";

Value RunGetLabelInfo(Scope* scope,
                      const FunctionCallNode* function,
                      const std::vector<Value>& args,
                      Err* err) {
  if (args.size() != 2) {
    *err = Err(function, "Expected two arguments.");
    return Value();
  }

  // Resolve the requested label.
  Label label = Label::Resolve(scope->GetSourceDir(),
                               ToolchainLabelForScope(scope), args[0], err);
  if (label.is_null())
    return Value();

  // Extract the "what" parameter.
  if (!args[1].VerifyTypeIs(Value::STRING, err))
    return Value();
  const std::string& what = args[1].string_value();

  Value result(function, Value::STRING);
  if (what == "name") {
    result.string_value() = label.name();

  } else if (what == "dir") {
    result.string_value() = DirectoryWithNoLastSlash(label.dir());

  } else if (what == "target_gen_dir") {
    result.string_value() = DirectoryWithNoLastSlash(GetSubBuildDirAsSourceDir(
        BuildDirContext(scope, label.GetToolchainLabel()), label.dir(),
        BuildDirType::GEN));

  } else if (what == "root_gen_dir") {
    result.string_value() = DirectoryWithNoLastSlash(GetBuildDirAsSourceDir(
        BuildDirContext(scope, label.GetToolchainLabel()), BuildDirType::GEN));

  } else if (what == "target_out_dir") {
    result.string_value() = DirectoryWithNoLastSlash(GetSubBuildDirAsSourceDir(
        BuildDirContext(scope, label.GetToolchainLabel()), label.dir(),
        BuildDirType::OBJ));

  } else if (what == "root_out_dir") {
    result.string_value() = DirectoryWithNoLastSlash(GetBuildDirAsSourceDir(
        BuildDirContext(scope, label.GetToolchainLabel()),
        BuildDirType::TOOLCHAIN_ROOT));

  } else if (what == "toolchain") {
    result.string_value() = label.GetToolchainLabel().GetUserVisibleName(false);

  } else if (what == "label_no_toolchain") {
    result.string_value() =
        label.GetWithNoToolchain().GetUserVisibleName(false);

  } else if (what == "label_with_toolchain") {
    result.string_value() = label.GetUserVisibleName(true);

  } else {
    *err = Err(args[1], "Unknown value for \"what\" parameter.");
    return Value();
  }

  return result;
}

}  // namespace functions
