// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"

#include <stdlib.h>

#include "base/base_paths_fuchsia.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/process/process.h"

namespace base {

base::FilePath GetPackageRoot() {
  return base::FilePath("/pkg");
}

bool PathProviderFuchsia(int key, FilePath* result) {
  switch (key) {
    case FILE_MODULE:
      NOTIMPLEMENTED();
      return false;
    case FILE_EXE:
      *result = CommandLine::ForCurrentProcess()->GetProgram();
      return true;
    case DIR_APP_DATA:
      // TODO(https://crbug.com/840598): Switch to /data when minfs supports
      // mmap().
      DLOG(WARNING) << "Using /tmp as app data dir, changes will NOT be "
                       "persisted! (crbug.com/840598)";
      *result = FilePath("/tmp");
      return true;
    case DIR_CACHE:
      *result = FilePath("/data");
      return true;
    case DIR_ASSETS:
    case DIR_SOURCE_ROOT:
      *result = GetPackageRoot();
      return true;
  }
  return false;
}

}  // namespace base
