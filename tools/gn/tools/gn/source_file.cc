// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/source_file.h"

#include "base/logging.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/source_dir.h"
#include "util/build_config.h"

namespace {

void AssertValueSourceFileString(const std::string& s) {
#if defined(OS_WIN)
  DCHECK(s[0] == '/' ||
         (s.size() > 2 && s[0] != '/' && s[1] == ':' && IsSlash(s[2])));
#else
  DCHECK(s[0] == '/');
#endif
  DCHECK(!EndsWithSlash(s)) << s;
}

}  // namespace

SourceFile::SourceFile() = default;

SourceFile::SourceFile(const base::StringPiece& p)
    : value_(p.data(), p.size()) {
  DCHECK(!value_.empty());
  AssertValueSourceFileString(value_);
  NormalizePath(&value_);
}

SourceFile::SourceFile(SwapIn, std::string* value) {
  value_.swap(*value);
  DCHECK(!value_.empty());
  AssertValueSourceFileString(value_);
  NormalizePath(&value_);
}

SourceFile::~SourceFile() = default;

std::string SourceFile::GetName() const {
  if (is_null())
    return std::string();

  DCHECK(value_.find('/') != std::string::npos);
  size_t last_slash = value_.rfind('/');
  return std::string(&value_[last_slash + 1], value_.size() - last_slash - 1);
}

SourceDir SourceFile::GetDir() const {
  if (is_null())
    return SourceDir();

  DCHECK(value_.find('/') != std::string::npos);
  size_t last_slash = value_.rfind('/');
  return SourceDir(base::StringPiece(&value_[0], last_slash + 1));
}

base::FilePath SourceFile::Resolve(const base::FilePath& source_root) const {
  return ResolvePath(value_, true, source_root);
}
