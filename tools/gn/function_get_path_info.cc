// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "tools/gn/err.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/functions.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"
#include "tools/gn/value.h"

namespace functions {

namespace {

// Corresponds to the various values of "what" in the function call.
enum What {
  WHAT_FILE,
  WHAT_NAME,
  WHAT_EXTENSION,
  WHAT_DIR,
  WHAT_ABSPATH,
  WHAT_GEN_DIR,
  WHAT_OUT_DIR,
};

// Returns the directory containing the input (resolving it against the
// |current_dir|), regardless of whether the input is a directory or a file.
SourceDir DirForInput(const Settings* settings,
                      const SourceDir& current_dir,
                      const Value& input,
                      Err* err) {
  // Input should already have been validated as a string.
  const std::string& input_string = input.string_value();

  if (!input_string.empty() && input_string[input_string.size() - 1] == '/') {
    // Input is a directory.
    return current_dir.ResolveRelativeDir(input, err,
        settings->build_settings()->root_path_utf8());
  }

  // Input is a directory.
  return current_dir.ResolveRelativeFile(input, err,
      settings->build_settings()->root_path_utf8()).GetDir();
}

std::string GetOnePathInfo(const Settings* settings,
                           const SourceDir& current_dir,
                           What what,
                           const Value& input,
                           Err* err) {
  if (!input.VerifyTypeIs(Value::STRING, err))
    return std::string();
  const std::string& input_string = input.string_value();
  if (input_string.empty()) {
    *err = Err(input, "Calling get_path_info on an empty string.");
    return std::string();
  }

  switch (what) {
    case WHAT_FILE: {
      return FindFilename(&input_string).as_string();
    }
    case WHAT_NAME: {
      std::string file = FindFilename(&input_string).as_string();
      size_t extension_offset = FindExtensionOffset(file);
      if (extension_offset == std::string::npos)
        return file;
      // Trim extension and dot.
      return file.substr(0, extension_offset - 1);
    }
    case WHAT_EXTENSION: {
      return FindExtension(&input_string).as_string();
    }
    case WHAT_DIR: {
      base::StringPiece dir_incl_slash = FindDir(&input_string);
      if (dir_incl_slash.empty())
        return std::string(".");
      // Trim slash since this function doesn't return trailing slashes. The
      // times we don't do this are if the result is "/" and "//" since those
      // slashes can't be trimmed.
      if (dir_incl_slash == "/")
        return std::string("/.");
      if (dir_incl_slash == "//")
        return std::string("//.");
      return dir_incl_slash.substr(0, dir_incl_slash.size() - 1).as_string();
    }
    case WHAT_GEN_DIR: {
      return DirectoryWithNoLastSlash(GetSubBuildDirAsSourceDir(
          BuildDirContext(settings),
          DirForInput(settings, current_dir, input, err),
          BuildDirType::GEN));
    }
    case WHAT_OUT_DIR: {
      return DirectoryWithNoLastSlash(GetSubBuildDirAsSourceDir(
          BuildDirContext(settings),
          DirForInput(settings, current_dir, input, err),
          BuildDirType::OBJ));
    }
    case WHAT_ABSPATH: {
      if (!input_string.empty() &&
          input_string[input_string.size() - 1] == '/') {
        return current_dir.ResolveRelativeDir(input, err,
            settings->build_settings()->root_path_utf8()).value();
      } else {
        return current_dir.ResolveRelativeFile(input, err,
            settings->build_settings()->root_path_utf8()).value();
      }
    }
    default:
      NOTREACHED();
      return std::string();
  }
}

}  // namespace

const char kGetPathInfo[] = "get_path_info";
const char kGetPathInfo_HelpShort[] =
    "get_path_info: Extract parts of a file or directory name.";
const char kGetPathInfo_Help[] =
    R"(get_path_info: Extract parts of a file or directory name.

  get_path_info(input, what)

  The first argument is either a string representing a file or directory name,
  or a list of such strings. If the input is a list the return value will be a
  list containing the result of applying the rule to each item in the input.

Possible values for the "what" parameter

  "file"
      The substring after the last slash in the path, including the name and
      extension. If the input ends in a slash, the empty string will be
      returned.
        "foo/bar.txt" => "bar.txt"
        "bar.txt" => "bar.txt"
        "foo/" => ""
        "" => ""

  "name"
     The substring of the file name not including the extension.
        "foo/bar.txt" => "bar"
        "foo/bar" => "bar"
        "foo/" => ""

  "extension"
      The substring following the last period following the last slash, or the
      empty string if not found. The period is not included.
        "foo/bar.txt" => "txt"
        "foo/bar" => ""

  "dir"
      The directory portion of the name, not including the slash.
        "foo/bar.txt" => "foo"
        "//foo/bar" => "//foo"
        "foo" => "."

      The result will never end in a slash, so if the resulting is empty, the
      system ("/") or source ("//") roots, a "." will be appended such that it
      is always legal to append a slash and a filename and get a valid path.

  "out_dir"
      The output file directory corresponding to the path of the given file,
      not including a trailing slash.
        "//foo/bar/baz.txt" => "//out/Default/obj/foo/bar"

  "gen_dir"
      The generated file directory corresponding to the path of the given file,
      not including a trailing slash.
        "//foo/bar/baz.txt" => "//out/Default/gen/foo/bar"

  "abspath"
      The full absolute path name to the file or directory. It will be resolved
      relative to the current directory, and then the source- absolute version
      will be returned. If the input is system- absolute, the same input will
      be returned.
        "foo/bar.txt" => "//mydir/foo/bar.txt"
        "foo/" => "//mydir/foo/"
        "//foo/bar" => "//foo/bar"  (already absolute)
        "/usr/include" => "/usr/include"  (already absolute)

      If you want to make the path relative to another directory, or to be
      system-absolute, see rebase_path().

Examples
  sources = [ "foo.cc", "foo.h" ]
  result = get_path_info(source, "abspath")
  # result will be [ "//mydir/foo.cc", "//mydir/foo.h" ]

  result = get_path_info("//foo/bar/baz.cc", "dir")
  # result will be "//foo/bar"

  # Extract the source-absolute directory name,
  result = get_path_info(get_path_info(path, "dir"), "abspath"
)";

Value RunGetPathInfo(Scope* scope,
                     const FunctionCallNode* function,
                     const std::vector<Value>& args,
                     Err* err) {
  if (args.size() != 2) {
    *err = Err(function, "Expecting two arguments to get_path_info.");
    return Value();
  }

  // Extract the "what".
  if (!args[1].VerifyTypeIs(Value::STRING, err))
    return Value();
  What what;
  if (args[1].string_value() == "file") {
    what = WHAT_FILE;
  } else if (args[1].string_value() == "name") {
    what = WHAT_NAME;
  } else if (args[1].string_value() == "extension") {
    what = WHAT_EXTENSION;
  } else if (args[1].string_value() == "dir") {
    what = WHAT_DIR;
  } else if (args[1].string_value() == "out_dir") {
    what = WHAT_OUT_DIR;
  } else if (args[1].string_value() == "gen_dir") {
    what = WHAT_GEN_DIR;
  } else if (args[1].string_value() == "abspath") {
    what = WHAT_ABSPATH;
  } else {
    *err = Err(args[1], "Unknown value for 'what'.");
    return Value();
  }

  const SourceDir& current_dir = scope->GetSourceDir();
  if (args[0].type() == Value::STRING) {
    return Value(function, GetOnePathInfo(scope->settings(), current_dir, what,
                                          args[0], err));
  } else if (args[0].type() == Value::LIST) {
    const std::vector<Value>& input_list = args[0].list_value();
    Value result(function, Value::LIST);
    for (const auto& cur : input_list) {
      result.list_value().push_back(Value(function,
          GetOnePathInfo(scope->settings(), current_dir, what, cur, err)));
      if (err->has_error())
        return Value();
    }
    return result;
  }

  *err = Err(args[0], "Path must be a string or a list of strings.");
  return Value();
}

}  // namespace functions
