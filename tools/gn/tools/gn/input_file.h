// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_INPUT_FILE_H_
#define TOOLS_GN_INPUT_FILE_H_

#include <string>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/macros.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/source_file.h"

class InputFile {
 public:
  explicit InputFile(const SourceFile& name);

  ~InputFile();

  // The virtual name passed into the constructor. This does not take into
  // account whether the file was loaded from the secondary source tree (see
  // BuildSettings secondary_source_path).
  const SourceFile& name() const { return name_; }

  // The directory is just a cached version of name()->GetDir() but we get this
  // a lot so computing it once up front saves a bunch of work.
  const SourceDir& dir() const { return dir_; }

  // The physical name tells the actual name on disk, if there is one.
  const base::FilePath& physical_name() const { return physical_name_; }

  // The friendly name can be set to override the name() in cases where there
  // is no name (like SetContents is used instead) or if the name doesn't
  // make sense. This will be displayed in error messages.
  const std::string& friendly_name() const { return friendly_name_; }
  void set_friendly_name(const std::string& f) { friendly_name_ = f; }

  const std::string& contents() const {
    DCHECK(contents_loaded_);
    return contents_;
  }

  // For testing and in cases where this input doesn't actually refer to
  // "a file".
  void SetContents(const std::string& c);

  // Loads the given file synchronously, returning true on success. This
  bool Load(const base::FilePath& system_path);

 private:
  SourceFile name_;
  SourceDir dir_;

  base::FilePath physical_name_;
  std::string friendly_name_;

  bool contents_loaded_;
  std::string contents_;

  DISALLOW_COPY_AND_ASSIGN(InputFile);
};

#endif  // TOOLS_GN_INPUT_FILE_H_
