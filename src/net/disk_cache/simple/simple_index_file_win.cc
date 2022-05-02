// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/simple/simple_index_file.h"

#include <string>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"

namespace disk_cache {

// static
bool SimpleIndexFile::TraverseCacheDirectory(
    const base::FilePath& cache_path,
    const EntryFileCallback& entry_file_callback) {
  const base::FilePath current_directory(FILE_PATH_LITERAL("."));
  const base::FilePath parent_directory(FILE_PATH_LITERAL(".."));
  const base::FilePath::StringType file_pattern = FILE_PATH_LITERAL("*");
  base::FileEnumerator enumerator(
      cache_path, false /* recursive */, base::FileEnumerator::FILES,
      file_pattern);
  for (base::FilePath file_path = enumerator.Next(); !file_path.empty();
       file_path = enumerator.Next()) {
    if (file_path == current_directory || file_path == parent_directory)
      continue;
    base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    entry_file_callback.Run(file_path, base::Time(), info.GetLastModifiedTime(),
                            info.GetSize());
  }
  return true;
}

}  // namespace disk_cache
