// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/output_file.h"

#include "tools/gn/filesystem_utils.h"
#include "tools/gn/source_file.h"

OutputFile::OutputFile() : value_() {
}

OutputFile::OutputFile(std::string&& v)
    : value_(v) {
}

OutputFile::OutputFile(const std::string& v)
    : value_(v) {
}

OutputFile::OutputFile(const BuildSettings* build_settings,
                       const SourceFile& source_file)
    : value_(RebasePath(source_file.value(),
                        build_settings->build_dir(),
                        build_settings->root_path_utf8())) {
}

OutputFile::~OutputFile() {
}

SourceFile OutputFile::AsSourceFile(const BuildSettings* build_settings) const {
  DCHECK(!value_.empty());
  DCHECK(value_[value_.size() - 1] != '/');

  std::string path = build_settings->build_dir().value();
  path.append(value_);
  NormalizePath(&path);
  return SourceFile(path);
}

SourceDir OutputFile::AsSourceDir(const BuildSettings* build_settings) const {
  if (!value_.empty()) {
    // Empty means the root build dir. Otherwise, we expect it to end in a
    // slash.
    DCHECK(value_[value_.size() - 1] == '/');
  }
  std::string path = build_settings->build_dir().value();
  path.append(value_);
  NormalizePath(&path);
  return SourceDir(path);
}
