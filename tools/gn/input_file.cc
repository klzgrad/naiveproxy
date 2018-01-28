// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/input_file.h"

#include "base/files/file_util.h"

InputFile::InputFile(const SourceFile& name)
    : name_(name),
      dir_(name_.GetDir()),
      contents_loaded_(false) {
}

InputFile::~InputFile() {
}

void InputFile::SetContents(const std::string& c) {
  contents_loaded_ = true;
  contents_ = c;
}

bool InputFile::Load(const base::FilePath& system_path) {
  if (base::ReadFileToString(system_path, &contents_)) {
    contents_loaded_ = true;
    physical_name_ = system_path;
    return true;
  }
  return false;
}

