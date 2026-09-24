/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_FILE_SYSTEM_VFS_H_
#define SRC_TRACE_PROCESSOR_SQLITE_FILE_SYSTEM_VFS_H_

#include <memory>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"

namespace perfetto::trace_processor {

namespace io {
class FileSystem;
}  // namespace io

// A scoped SQLite VFS backed by a Trace Processor FileSystem. Each instance has
// a unique SQLite VFS name and must outlive every SQLite database opened on it.
class SqliteFileSystemVfs {
 public:
  static base::StatusOr<std::unique_ptr<SqliteFileSystemVfs>> Create(
      io::FileSystem*);

  ~SqliteFileSystemVfs();

  SqliteFileSystemVfs(const SqliteFileSystemVfs&) = delete;
  SqliteFileSystemVfs& operator=(const SqliteFileSystemVfs&) = delete;

  const char* name() const;

 private:
  class Impl;

  explicit SqliteFileSystemVfs(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_SQLITE_FILE_SYSTEM_VFS_H_
