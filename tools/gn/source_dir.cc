// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/source_dir.h"

#include "base/logging.h"
#include "build/build_config.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/source_file.h"

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

}  // namespace

SourceDir::SourceDir() {
}

SourceDir::SourceDir(const base::StringPiece& p)
    : value_(p.data(), p.size()) {
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

SourceDir::~SourceDir() {
}

SourceFile SourceDir::ResolveRelativeFile(
    const Value& p,
    Err* err,
    const base::StringPiece& source_root) const {
  SourceFile ret;
  if (!p.VerifyTypeIs(Value::STRING, err))
    return ret;

  // It's an error to resolve an empty string or one that is a directory
  // (indicated by a trailing slash) because this is the function that expects
  // to return a file.
  const std::string& str = p.string_value();
  if (str.empty()) {
    *err = Err(p, "Empty file path.",
               "You can't use empty strings as file paths. That's just wrong.");
    return ret;
  } else if (str[str.size() - 1] == '/') {
    *err = Err(p, "File path ends in a slash.",
               "You specified the path\n  " + str + "\n"
               "and it ends in a slash, indicating you think it's a directory."
               "\nBut here you're supposed to be listing a file.");
    return ret;
  }

  if (str.size() >= 2 && str[0] == '/' && str[1] == '/') {
    // Source-relative.
    ret.value_.assign(str.data(), str.size());
    NormalizePath(&ret.value_, source_root);
    return ret;
  } else if (IsPathAbsolute(str)) {
    if (source_root.empty() ||
        !MakeAbsolutePathRelativeIfPossible(source_root, str, &ret.value_)) {
#if defined(OS_WIN)
      // On Windows we'll accept "C:\foo" as an absolute path, which we want
      // to convert to "/C:..." here.
      if (str[0] != '/')
        ret.value_ = "/";
#endif
      ret.value_.append(str.data(), str.size());
    }
    NormalizePath(&ret.value_);
    return ret;
  }

  if (!source_root.empty()) {
    std::string absolute =
        FilePathToUTF8(Resolve(UTF8ToFilePath(source_root)).AppendASCII(
            str).value());
    NormalizePath(&absolute);
    if (!MakeAbsolutePathRelativeIfPossible(source_root, absolute,
                                            &ret.value_)) {
#if defined(OS_WIN)
      // On Windows we'll accept "C:\foo" as an absolute path, which we want
      // to convert to "/C:..." here.
      if (absolute[0] != '/')
        ret.value_ = "/";
#endif
      ret.value_.append(absolute.data(), absolute.size());
    }
    return ret;
  }

  // With no source_root_, there's nothing we can do about
  // e.g. p=../../../path/to/file and value_=//source and we'll
  // errornously return //file.
  ret.value_.reserve(value_.size() + str.size());
  ret.value_.assign(value_);
  ret.value_.append(str.data(), str.size());

  NormalizePath(&ret.value_);
  return ret;
}

SourceDir SourceDir::ResolveRelativeDir(
    const Value& p,
    Err* err,
    const base::StringPiece& source_root) const {
  if (!p.VerifyTypeIs(Value::STRING, err))
    return SourceDir();
  return ResolveRelativeDir(p, p.string_value(), err, source_root);
}

SourceDir SourceDir::ResolveRelativeDir(
    const Value& blame_but_dont_use,
    const base::StringPiece& str,
    Err* err,
    const base::StringPiece& source_root) const {
  SourceDir ret;

  if (str.empty()) {
    *err = Err(blame_but_dont_use, "Empty directory path.",
               "You can't use empty strings as directories. "
               "That's just wrong.");
    return ret;
  }

  if (str.size() >= 2 && str[0] == '/' && str[1] == '/') {
    // Source-relative.
    ret.value_.assign(str.data(), str.size());
    if (!EndsWithSlash(ret.value_))
      ret.value_.push_back('/');
    NormalizePath(&ret.value_, source_root);
    return ret;
  } else if (IsPathAbsolute(str)) {
    if (source_root.empty() ||
        !MakeAbsolutePathRelativeIfPossible(source_root, str, &ret.value_)) {
#if defined(OS_WIN)
      if (str[0] != '/')  // See the file case for why we do this check.
        ret.value_ = "/";
#endif
      ret.value_.append(str.data(), str.size());
    }
    NormalizePath(&ret.value_);
    if (!EndsWithSlash(ret.value_))
      ret.value_.push_back('/');
    return ret;
  }

  if (!source_root.empty()) {
    std::string absolute =
        FilePathToUTF8(Resolve(UTF8ToFilePath(source_root)).AppendASCII(
            str.as_string()).value());
    NormalizePath(&absolute);
    if (!MakeAbsolutePathRelativeIfPossible(source_root, absolute,
                                            &ret.value_)) {
#if defined(OS_WIN)
      if (absolute[0] != '/')  // See the file case for why we do this check.
        ret.value_ = "/";
#endif
      ret.value_.append(absolute.data(), absolute.size());
    }
    if (!EndsWithSlash(ret.value_))
      ret.value_.push_back('/');
    return ret;
  }

  ret.value_.reserve(value_.size() + str.size());
  ret.value_.assign(value_);
  ret.value_.append(str.data(), str.size());

  NormalizePath(&ret.value_);
  if (!EndsWithSlash(ret.value_))
    ret.value_.push_back('/');
  AssertValueSourceDirString(ret.value_);

  return ret;
}

base::FilePath SourceDir::Resolve(const base::FilePath& source_root) const {
  if (is_null())
    return base::FilePath();

  std::string converted;
  if (is_system_absolute()) {
    if (value_.size() > 2 && value_[2] == ':') {
      // Windows path, strip the leading slash.
      converted.assign(&value_[1], value_.size() - 1);
    } else {
      converted.assign(value_);
    }
    return base::FilePath(UTF8ToFilePath(converted));
  }

  // String the double-leading slash for source-relative paths.
  converted.assign(&value_[2], value_.size() - 2);
  return source_root.Append(UTF8ToFilePath(converted))
      .NormalizePathSeparatorsTo('/');
}

void SourceDir::SwapValue(std::string* v) {
  value_.swap(*v);
  AssertValueSourceDirString(value_);
}
