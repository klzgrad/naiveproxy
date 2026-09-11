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

#include "src/trace_processor/local_file_system.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"

namespace perfetto::trace_processor::io {
namespace {

class LocalFile final : public File {
 public:
  LocalFile(std::string path, base::ScopedFile fd)
      : path_(std::move(path)), fd_(std::move(fd)) {}

  base::Status ReadAt(uint64_t offset,
                      void* data,
                      size_t size,
                      size_t* bytes_read) override {
    if (!base::SeekFile(fd_.get(), offset)) {
      return base::ErrStatus("Failed to seek file %s: %s", path_.c_str(),
                             strerror(errno));
    }
    size_t total = 0;
    auto* dst = static_cast<uint8_t*>(data);
    while (total < size) {
      ssize_t read = base::Read(fd_.get(), dst + total, size - total);
      if (read < 0) {
        return base::ErrStatus("Failed to read file %s: %s", path_.c_str(),
                               strerror(errno));
      }
      if (read == 0) {
        break;
      }
      total += static_cast<size_t>(read);
    }
    *bytes_read = total;
    return base::OkStatus();
  }

  base::Status WriteAt(uint64_t offset,
                       const void* data,
                       size_t size) override {
    if (!base::SeekFile(fd_.get(), offset)) {
      return base::ErrStatus("Failed to seek file %s: %s", path_.c_str(),
                             strerror(errno));
    }
    if (base::WriteAll(fd_.get(), data, size) != static_cast<ssize_t>(size)) {
      return base::ErrStatus("Failed to write file %s: %s", path_.c_str(),
                             strerror(errno));
    }
    return base::OkStatus();
  }

  base::Status Truncate(uint64_t size) override {
    if (!base::TruncateFile(fd_.get(), size)) {
      return base::ErrStatus("Failed to truncate file %s: %s", path_.c_str(),
                             strerror(errno));
    }
    return base::OkStatus();
  }

  base::Status GetSize(uint64_t* size) override {
    std::optional<uint64_t> result = base::GetFileSize(fd_.get());
    if (!result) {
      return base::ErrStatus("Failed to get size of file %s: %s", path_.c_str(),
                             strerror(errno));
    }
    *size = *result;
    return base::OkStatus();
  }

  base::Status Flush() override {
    if (!base::FlushFile(fd_.get())) {
      return base::ErrStatus("Failed to flush file %s: %s", path_.c_str(),
                             strerror(errno));
    }
    return base::OkStatus();
  }

 private:
  std::string path_;
  base::ScopedFile fd_;
};

class NoopFileSystem final : public FileSystem {
 public:
  base::Status OpenFile(const std::string&,
                        const FileOpenOptions&,
                        std::unique_ptr<File>*) override {
    return base::ErrStatus("File I/O is not supported");
  }

  base::Status DeleteFile(const std::string&) override {
    return base::ErrStatus("File I/O is not supported");
  }

  base::Status FileExists(const std::string&, bool*) override {
    return base::ErrStatus("File I/O is not supported");
  }
};

class LocalFileSystem final : public FileSystem {
 public:
  base::Status OpenFile(const std::string& path,
                        const FileOpenOptions& options,
                        std::unique_ptr<File>* file) override {
    if ((options.create || options.truncate) &&
        options.access == FileAccess::kReadOnly) {
      return base::ErrStatus(
          "Creating or truncating a file requires write access");
    }

    int flags = 0;
    switch (options.access) {
      case FileAccess::kReadOnly:
        flags = O_RDONLY;
        break;
      case FileAccess::kWriteOnly:
        flags = O_WRONLY;
        break;
      case FileAccess::kReadWrite:
        flags = O_RDWR;
        break;
    }
    if (options.create) {
      flags |= O_CREAT;
    }
    if (options.truncate) {
      flags |= O_TRUNC;
    }

    base::ScopedFile fd = base::OpenFile(path, flags, 0600);
    if (!fd) {
      return base::ErrStatus("Failed to open file %s: %s", path.c_str(),
                             strerror(errno));
    }
    file->reset(new LocalFile(path, std::move(fd)));
    return base::OkStatus();
  }

  base::Status DeleteFile(const std::string& path) override {
    if (!base::Unlink(path.c_str())) {
      return base::ErrStatus("Failed to delete file %s: %s", path.c_str(),
                             strerror(errno));
    }
    return base::OkStatus();
  }

  base::Status FileExists(const std::string& path, bool* exists) override {
    *exists = base::FileExists(path);
    return base::OkStatus();
  }
};

}  // namespace

FileSystem* CreateLocalFileSystem() {
  static LocalFileSystem* file_system = new LocalFileSystem();
  return file_system;
}

FileSystem* CreateNoopFileSystem() {
  static NoopFileSystem* file_system = new NoopFileSystem();
  return file_system;
}

}  // namespace perfetto::trace_processor::io
