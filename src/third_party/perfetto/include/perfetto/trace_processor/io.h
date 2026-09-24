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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_IO_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_IO_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "perfetto/base/export.h"
#include "perfetto/base/status.h"

namespace perfetto::trace_processor::io {

// A synchronous random-access file. Each instance is used from a single thread
// and implementations do not need to support concurrent calls.
class PERFETTO_EXPORT_COMPONENT File {
 public:
  File();
  virtual ~File();

  File(const File&) = delete;
  File& operator=(const File&) = delete;

  // Reads up to |size| bytes starting at |offset|. A short read indicates EOF.
  virtual base::Status ReadAt(uint64_t offset,
                              void* data,
                              size_t size,
                              size_t* bytes_read) = 0;

  // Writes all |size| bytes starting at |offset|.
  virtual base::Status WriteAt(uint64_t offset,
                               const void* data,
                               size_t size) = 0;

  virtual base::Status Truncate(uint64_t size) = 0;
  virtual base::Status GetSize(uint64_t* size) = 0;
  virtual base::Status Flush() = 0;
};

enum class FileAccess {
  kReadOnly,
  kWriteOnly,
  kReadWrite,
};

struct FileOpenOptions {
  FileAccess access = FileAccess::kReadOnly;
  bool create = false;
  bool truncate = false;
};

// A synchronous filesystem used by Trace Processor features which need named
// files. Paths are opaque to Trace Processor and interpreted by the embedder.
// Implementations must be thread-safe, although each returned File is only
// used from one thread.
class PERFETTO_EXPORT_COMPONENT FileSystem {
 public:
  FileSystem();
  virtual ~FileSystem();

  FileSystem(const FileSystem&) = delete;
  FileSystem& operator=(const FileSystem&) = delete;

  // Opens |path| with the given |options|. On success, stores the opened
  // random-access file in |file|.
  virtual base::Status OpenFile(const std::string& path,
                                const FileOpenOptions& options,
                                std::unique_ptr<File>* file) = 0;

  virtual base::Status DeleteFile(const std::string& path) = 0;
  virtual base::Status FileExists(const std::string& path, bool* exists) = 0;
};

}  // namespace perfetto::trace_processor::io

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_IO_H_
