// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "tools/gn/build_settings.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/functions.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"
#include "tools/gn/settings.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/source_file.h"
#include "tools/gn/value.h"

namespace functions {

namespace {

// We want the output to match the input in terms of ending in a slash or not.
// Through all the transformations, these can get added or removed in various
// cases.
void MakeSlashEndingMatchInput(const std::string& input, std::string* output) {
  if (EndsWithSlash(input)) {
    if (!EndsWithSlash(*output))  // Preserve same slash type as input.
      output->push_back(input[input.size() - 1]);
  } else {
    if (EndsWithSlash(*output))
      output->resize(output->size() - 1);
  }
}

// Returns true if the given value looks like a directory, otherwise we'll
// assume it's a file.
bool ValueLooksLikeDir(const std::string& value) {
  if (value.empty())
    return true;
  size_t value_size = value.size();

  // Count the number of dots at the end of the string.
  size_t num_dots = 0;
  while (num_dots < value_size && value[value_size - num_dots - 1] == '.')
    num_dots++;

  if (num_dots == value.size())
    return true;  // String is all dots.

  if (IsSlash(value[value_size - num_dots - 1]))
    return true;  // String is a [back]slash followed by 0 or more dots.

  // Anything else.
  return false;
}

Value ConvertOnePath(const Scope* scope,
                     const FunctionCallNode* function,
                     const Value& value,
                     const SourceDir& from_dir,
                     const SourceDir& to_dir,
                     bool convert_to_system_absolute,
                     Err* err) {
  Value result;  // Ensure return value optimization.

  if (!value.VerifyTypeIs(Value::STRING, err))
    return result;
  const std::string& string_value = value.string_value();

  bool looks_like_dir = ValueLooksLikeDir(string_value);

  // System-absolute output special case.
  if (convert_to_system_absolute) {
    base::FilePath system_path;
    if (looks_like_dir) {
      system_path = scope->settings()->build_settings()->GetFullPath(
          from_dir.ResolveRelativeDir(value, err,
              scope->settings()->build_settings()->root_path_utf8()));
    } else {
      system_path = scope->settings()->build_settings()->GetFullPath(
          from_dir.ResolveRelativeFile(value, err,
              scope->settings()->build_settings()->root_path_utf8()));
    }
    if (err->has_error())
      return Value();

    result = Value(function, FilePathToUTF8(system_path));
    if (looks_like_dir)
      MakeSlashEndingMatchInput(string_value, &result.string_value());
    return result;
  }

  result = Value(function, Value::STRING);
  if (looks_like_dir) {
    result.string_value() = RebasePath(
        from_dir.ResolveRelativeDir(value, err,
            scope->settings()->build_settings()->root_path_utf8()).value(),
        to_dir,
        scope->settings()->build_settings()->root_path_utf8());
    MakeSlashEndingMatchInput(string_value, &result.string_value());
  } else {
    SourceFile resolved_file =
        from_dir.ResolveRelativeFile(value, err,
            scope->settings()->build_settings()->root_path_utf8());
    if (err->has_error())
      return Value();
    // Special case:
    //   rebase_path("//foo", "//bar") ==> "../foo"
    //   rebase_path("//foo", "//foo") ==> "." and not "../foo"
    if (resolved_file.value() ==
        to_dir.value().substr(0, to_dir.value().size() - 1)) {
      result.string_value() = ".";
    } else {
      result.string_value() = RebasePath(
          resolved_file.value(),
          to_dir,
          scope->settings()->build_settings()->root_path_utf8());
    }
  }

  return result;
}

}  // namespace

const char kRebasePath[] = "rebase_path";
const char kRebasePath_HelpShort[] =
    "rebase_path: Rebase a file or directory to another location.";
const char kRebasePath_Help[] =
    R"(rebase_path: Rebase a file or directory to another location.

  converted = rebase_path(input,
                          new_base = "",
                          current_base = ".")

  Takes a string argument representing a file name, or a list of such strings
  and converts it/them to be relative to a different base directory.

  When invoking the compiler or scripts, GN will automatically convert sources
  and include directories to be relative to the build directory. However, if
  you're passing files directly in the "args" array or doing other manual
  manipulations where GN doesn't know something is a file name, you will need
  to convert paths to be relative to what your tool is expecting.

  The common case is to use this to convert paths relative to the current
  directory to be relative to the build directory (which will be the current
  directory when executing scripts).

  If you want to convert a file path to be source-absolute (that is, beginning
  with a double slash like "//foo/bar"), you should use the get_path_info()
  function. This function won't work because it will always make relative
  paths, and it needs to support making paths relative to the source root, so
  can't also generate source-absolute paths without more special-cases.

Arguments

  input
      A string or list of strings representing file or directory names These
      can be relative paths ("foo/bar.txt"), system absolute paths
      ("/foo/bar.txt"), or source absolute paths ("//foo/bar.txt").

  new_base
      The directory to convert the paths to be relative to. This can be an
      absolute path or a relative path (which will be treated as being relative
      to the current BUILD-file's directory).

      As a special case, if new_base is the empty string (the default), all
      paths will be converted to system-absolute native style paths with system
      path separators. This is useful for invoking external programs.

  current_base
      Directory representing the base for relative paths in the input. If this
      is not an absolute path, it will be treated as being relative to the
      current build file. Use "." (the default) to convert paths from the
      current BUILD-file's directory.

Return value

  The return value will be the same type as the input value (either a string or
  a list of strings). All relative and source-absolute file names will be
  converted to be relative to the requested output System-absolute paths will
  be unchanged.

  Whether an output path will end in a slash will match whether the
  corresponding input path ends in a slash. It will return "." or "./"
  (depending on whether the input ends in a slash) to avoid returning empty
  strings. This means if you want a root path ("//" or "/") not ending in a
  slash, you can add a dot ("//.").

Example

  # Convert a file in the current directory to be relative to the build
  # directory (the current dir when executing compilers and scripts).
  foo = rebase_path("myfile.txt", root_build_dir)
  # might produce "../../project/myfile.txt".

  # Convert a file to be system absolute:
  foo = rebase_path("myfile.txt")
  # Might produce "D:\\source\\project\\myfile.txt" on Windows or
  # "/home/you/source/project/myfile.txt" on Linux.

  # Typical usage for converting to the build directory for a script.
  action("myscript") {
    # Don't convert sources, GN will automatically convert these to be relative
    # to the build directory when it constructs the command line for your
    # script.
    sources = [ "foo.txt", "bar.txt" ]

    # Extra file args passed manually need to be explicitly converted
    # to be relative to the build directory:
    args = [
      "--data",
      rebase_path("//mything/data/input.dat", root_build_dir),
      "--rel",
      rebase_path("relative_path.txt", root_build_dir)
    ] + rebase_path(sources, root_build_dir)
  }
)";

Value RunRebasePath(Scope* scope,
                    const FunctionCallNode* function,
                    const std::vector<Value>& args,
                    Err* err) {
  Value result;

  // Argument indices.
  static const size_t kArgIndexInputs = 0;
  static const size_t kArgIndexDest = 1;
  static const size_t kArgIndexFrom = 2;

  // Inputs.
  if (args.size() < 1 || args.size() > 3) {
    *err = Err(function->function(), "Wrong # of arguments for rebase_path.");
    return result;
  }
  const Value& inputs = args[kArgIndexInputs];

  // To path.
  bool convert_to_system_absolute = true;
  SourceDir to_dir;
  const SourceDir& current_dir = scope->GetSourceDir();
  if (args.size() > kArgIndexDest) {
    if (!args[kArgIndexDest].VerifyTypeIs(Value::STRING, err))
      return result;
    if (!args[kArgIndexDest].string_value().empty()) {
      to_dir = current_dir.ResolveRelativeDir(
          args[kArgIndexDest], err,
          scope->settings()->build_settings()->root_path_utf8());
      if (err->has_error())
        return Value();
      convert_to_system_absolute = false;
    }
  }

  // From path.
  SourceDir from_dir;
  if (args.size() > kArgIndexFrom) {
    if (!args[kArgIndexFrom].VerifyTypeIs(Value::STRING, err))
      return result;
    from_dir = current_dir.ResolveRelativeDir(
        args[kArgIndexFrom], err,
        scope->settings()->build_settings()->root_path_utf8());
    if (err->has_error())
      return Value();
  } else {
    // Default to current directory if unspecified.
    from_dir = current_dir;
  }

  // Path conversion.
  if (inputs.type() == Value::STRING) {
    return ConvertOnePath(scope, function, inputs,
                          from_dir, to_dir, convert_to_system_absolute, err);

  } else if (inputs.type() == Value::LIST) {
    result = Value(function, Value::LIST);
    result.list_value().reserve(inputs.list_value().size());

    for (const auto& input : inputs.list_value()) {
      result.list_value().push_back(
          ConvertOnePath(scope, function, input,
                         from_dir, to_dir, convert_to_system_absolute, err));
      if (err->has_error()) {
        result = Value();
        return result;
      }
    }
    return result;
  }

  *err = Err(function->function(),
             "rebase_path requires a list or a string.");
  return result;
}

}  // namespace functions
