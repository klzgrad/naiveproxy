// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"
#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/functions.h"
#include "tools/gn/input_conversion.h"
#include "tools/gn/input_file.h"
#include "tools/gn/scheduler.h"

// TODO(brettw) consider removing this. I originally wrote it for making the
// WebKit bindings but misundersood what was required, and didn't need to
// use this. This seems to have a high potential for misuse.

namespace functions {

const char kReadFile[] = "read_file";
const char kReadFile_HelpShort[] =
    "read_file: Read a file into a variable.";
const char kReadFile_Help[] =
    R"(read_file: Read a file into a variable.

  read_file(filename, input_conversion)

  Whitespace will be trimmed from the end of the file. Throws an error if the
  file can not be opened.

Arguments

  filename
      Filename to read, relative to the build file.

  input_conversion
      Controls how the file is read and parsed. See "gn help input_conversion".

Example

  lines = read_file("foo.txt", "list lines")
)";

Value RunReadFile(Scope* scope,
                  const FunctionCallNode* function,
                  const std::vector<Value>& args,
                  Err* err) {
  if (args.size() != 2) {
    *err = Err(function->function(), "Wrong number of arguments to read_file",
               "I expected two arguments.");
    return Value();
  }
  if (!args[0].VerifyTypeIs(Value::STRING, err))
    return Value();

  // Compute the file name.
  const SourceDir& cur_dir = scope->GetSourceDir();
  SourceFile source_file = cur_dir.ResolveRelativeFile(args[0], err,
      scope->settings()->build_settings()->root_path_utf8());
  if (err->has_error())
    return Value();
  base::FilePath file_path =
      scope->settings()->build_settings()->GetFullPath(source_file);

  // Ensure that everything is recomputed if the read file changes.
  g_scheduler->AddGenDependency(file_path);

  // Read contents.
  std::string file_contents;
  if (!base::ReadFileToString(file_path, &file_contents)) {
    *err = Err(args[0], "Could not read file.",
               "I resolved this to \"" + FilePathToUTF8(file_path) + "\".");
    return Value();
  }

  return ConvertInputToValue(scope->settings(), file_contents, function,
                             args[1], err);
}

}  // namespace functions
