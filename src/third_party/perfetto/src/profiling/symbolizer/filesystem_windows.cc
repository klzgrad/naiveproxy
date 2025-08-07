/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/profiling/symbolizer/filesystem.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

#include <Windows.h>

namespace perfetto {
namespace profiling {

bool WalkDirectories(std::vector<std::string> dirs, FileCallback fn) {
  std::vector<std::string> sub_dirs;
  for (const std::string& dir : dirs) {
    WIN32_FIND_DATAA file;
    HANDLE fh = FindFirstFileA((dir + "\\*").c_str(), &file);
    if (fh != INVALID_HANDLE_VALUE) {
      do {
        std::string file_path = dir + "\\" + file.cFileName;
        if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          if (strcmp(file.cFileName, ".") != 0 &&
              strcmp(file.cFileName, "..") != 0) {
            sub_dirs.push_back(file_path);
          }
        } else {
          ULARGE_INTEGER size;
          size.HighPart = file.nFileSizeHigh;
          size.LowPart = file.nFileSizeLow;
          fn(file_path.c_str(), size.QuadPart);
        }
      } while (FindNextFileA(fh, &file));
    }
    FindClose(fh);
  }
  if (!sub_dirs.empty()) {
    WalkDirectories(sub_dirs, fn);
  }
  return true;
}

}  // namespace profiling
}  // namespace perfetto

#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
