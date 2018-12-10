// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/source_dir.h"

#include "base/logging.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/source_file.h"
#include "util/build_config.h"

namespace {

void AssertValueSourceDirString(const std::string& s) {
  if (!s.empty()) {
#if defined(OS_WIN)
    DCHECK(s[0] == '/' ||
           (s.size() > 2 && s[0] != '/' && s[1] == ':' && IsSlash(s[2])));
#else
    DCHECK(s[0] == '/');
#endif
    DCHECK(EndsWithSlash(s)) << s;
  }
}

// Validates input value (input_value) and sets proper error message.
// Note: Parameter blame_input is used only for generating error message.
template <typename StringType>
bool ValidateResolveInput(bool as_file,
                          const Value& blame_input_value,
                          const StringType& input_value,
                          Err* err) {
  if (as_file) {
    // It's an error to resolve an empty string or one that is a directory
    // (indicated by a trailing slash) because this is the function that expects
    // to return a file.
    if (input_value.empty()) {
      *err = Err(blame_input_value, "Empty file path.",
                 "You can't use empty strings as file paths.");
      return false;
    } else if (input_value[input_value.size() - 1] == '/') {
      std::string help = "You specified the path\n  ";
      help.append(std::string(input_value));
      help.append(
          "\nand it ends in a slash, indicating you think it's a directory."
          "\nBut here you're supposed to be listing a file.");
      *err = Err(blame_input_value, "File path ends in a slash.", help);
      return false;
    }
  } else if (input_value.empty()) {
    *err = Err(blame_input_value, "Empty directory path.",
               "You can't use empty strings as directories.");
    return false;
  }
  return true;
}

}  // namespace

SourceDir::SourceDir() = default;

SourceDir::SourceDir(const base::StringPiece& p) : value_(p.data(), p.size()) {
  if (!EndsWithSlash(value_))
    value_.push_back('/');
  AssertValueSourceDirString(value_);
}

SourceDir::SourceDir(SwapIn, std::string* s) {
  value_.swap(*s);
  if (!EndsWithSlash(value_))
    value_.push_back('/');
  AssertValueSourceDirString(value_);
}

SourceDir::~SourceDir() = default;

template <typename StringType>
std::string SourceDir::ResolveRelativeAs(
    bool as_file,
    const Value& blame_input_value,
    const StringType& input_value,
    Err* err,
    const base::StringPiece& source_root) const {
  if (!ValidateResolveInput<StringType>(as_file, blame_input_value, input_value,
                                        err)) {
    return std::string();
  }
  return ResolveRelative(input_value, value_, as_file, source_root);
}

SourceFile SourceDir::ResolveRelativeFile(
    const Value& p,
    Err* err,
    const base::StringPiece& source_root) const {
  SourceFile ret;

  if (!p.VerifyTypeIs(Value::STRING, err))
    return ret;

  const std::string& input_string = p.string_value();
  if (!ValidateResolveInput<std::string>(true, p, input_string, err)) {
    return ret;
  }
  ret.value_ = ResolveRelative(input_string, value_, true, source_root);
  return ret;
}

std::string SourceDir::ResolveRelativeAs(bool as_file,
                                         const Value& v,
                                         Err* err,
                                         const base::StringPiece& source_root,
                                         const std::string* v_value) const {
  if (!v.VerifyTypeIs(Value::STRING, err))
    return std::string();

  if (!v_value) {
    v_value = &v.string_value();
  }
  std::string result =
      ResolveRelativeAs(as_file, v, *v_value, err, source_root);
  if (!as_file)
    AssertValueSourceDirString(result);
  return result;
}

SourceDir SourceDir::ResolveRelativeDir(
    const Value& v,
    Err* err,
    const base::StringPiece& source_root) const {
  if (!v.VerifyTypeIs(Value::STRING, err))
    return SourceDir();

  return ResolveRelativeDir<std::string>(v, v.string_value(), err, source_root);
}

base::FilePath SourceDir::Resolve(const base::FilePath& source_root) const {
  return ResolvePath(value_, false, source_root);
}

void SourceDir::SwapValue(std::string* v) {
  value_.swap(*v);
  AssertValueSourceDirString(value_);
}

// Explicit template instantiation
template std::string SourceDir::ResolveRelativeAs(
    bool as_file,
    const Value& blame_input_value,
    const std::string& input_value,
    Err* err,
    const base::StringPiece& source_root) const;

template std::string SourceDir::ResolveRelativeAs(
    bool as_file,
    const Value& blame_input_value,
    const base::StringPiece& input_value,
    Err* err,
    const base::StringPiece& source_root) const;
