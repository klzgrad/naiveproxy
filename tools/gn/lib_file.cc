// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/lib_file.h"

#include "base/logging.h"

LibFile::LibFile() {}

LibFile::LibFile(const SourceFile& source_file) : source_file_(source_file) {}

LibFile::LibFile(const base::StringPiece& lib_name)
    : name_(lib_name.data(), lib_name.size()) {
  DCHECK(!lib_name.empty());
}

void LibFile::Swap(LibFile* other) {
  name_.swap(other->name_);
  source_file_.swap(other->source_file_);
}

const std::string& LibFile::value() const {
  return is_source_file() ? source_file_.value() : name_;
}

const SourceFile& LibFile::source_file() const {
  DCHECK(is_source_file());
  return source_file_;
}
