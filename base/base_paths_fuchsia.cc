// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"

#include <stdlib.h>

#include "base/command_line.h"
#include "base/files/file_path.h"

namespace base {

bool PathProviderFuchsia(int key, FilePath* result) {
  switch (key) {
    case FILE_MODULE:
    // Not supported in debug or component builds. Fall back on using the EXE
    // path for now.
    // TODO(fuchsia): Get this value from an API. See crbug.com/726124
    case FILE_EXE: {
      // Use the binary name as specified on the command line.
      // TODO(fuchsia): It would be nice to get the canonical executable path
      // from a kernel API. See https://crbug.com/726124
      char bin_dir[PATH_MAX + 1];
      if (realpath(base::CommandLine::ForCurrentProcess()
                       ->GetProgram()
                       .AsUTF8Unsafe()
                       .c_str(),
                   bin_dir) == NULL) {
        return false;
      }
      *result = FilePath(bin_dir);
      return true;
    }
    case DIR_SOURCE_ROOT:
      // This is only used for tests, so we return the binary location for now.
      *result = FilePath("/system");
      return true;
    case DIR_CACHE:
      *result = FilePath("/data");
      return true;
  }

  return false;
}

}  // namespace base
