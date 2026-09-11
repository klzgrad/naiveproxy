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

#include "src/trace_processor/sqlite/file_system_vfs.h"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/io.h"

namespace perfetto::trace_processor {
namespace {

std::atomic<uint64_t> g_next_vfs_id{0};

struct OpenFile {
  sqlite3_file sqlite_file{};
  std::unique_ptr<io::File> file;
  io::FileSystem* file_system = nullptr;
  std::string path;
  bool delete_on_close = false;
};

OpenFile* ToOpenFile(sqlite3_file* file) {
  return reinterpret_cast<OpenFile*>(file);
}

int Close(sqlite3_file* sqlite_file) {
  OpenFile* file = ToOpenFile(sqlite_file);
  file->file.reset();
  if (file->delete_on_close) {
    base::ignore_result(file->file_system->DeleteFile(file->path));
  }
  file->~OpenFile();
  return SQLITE_OK;
}

int Read(sqlite3_file* sqlite_file,
         void* data,
         int size,
         sqlite3_int64 offset) {
  if (size < 0 || offset < 0) {
    return SQLITE_IOERR_READ;
  }
  OpenFile* file = ToOpenFile(sqlite_file);
  size_t bytes_read = 0;
  if (!file->file
           ->ReadAt(static_cast<uint64_t>(offset), data,
                    static_cast<size_t>(size), &bytes_read)
           .ok()) {
    return SQLITE_IOERR_READ;
  }
  if (bytes_read != static_cast<size_t>(size)) {
    auto* bytes = static_cast<uint8_t*>(data);
    std::fill(bytes + bytes_read, bytes + size, 0);
    return SQLITE_IOERR_SHORT_READ;
  }
  return SQLITE_OK;
}

int Write(sqlite3_file* sqlite_file,
          const void* data,
          int size,
          sqlite3_int64 offset) {
  if (size < 0 || offset < 0) {
    return SQLITE_IOERR_WRITE;
  }
  base::Status status = ToOpenFile(sqlite_file)
                            ->file->WriteAt(static_cast<uint64_t>(offset), data,
                                            static_cast<size_t>(size));
  return status.ok() ? SQLITE_OK : SQLITE_IOERR_WRITE;
}

int Truncate(sqlite3_file* sqlite_file, sqlite3_int64 size) {
  if (size < 0) {
    return SQLITE_IOERR_TRUNCATE;
  }
  base::Status status =
      ToOpenFile(sqlite_file)->file->Truncate(static_cast<uint64_t>(size));
  return status.ok() ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

int Sync(sqlite3_file* sqlite_file, int) {
  return ToOpenFile(sqlite_file)->file->Flush().ok() ? SQLITE_OK
                                                     : SQLITE_IOERR_FSYNC;
}

int FileSize(sqlite3_file* sqlite_file, sqlite3_int64* size) {
  uint64_t result = 0;
  if (!ToOpenFile(sqlite_file)->file->GetSize(&result).ok() ||
      result >
          static_cast<uint64_t>(std::numeric_limits<sqlite3_int64>::max())) {
    return SQLITE_IOERR_FSTAT;
  }
  *size = static_cast<sqlite3_int64>(result);
  return SQLITE_OK;
}

int Lock(sqlite3_file*, int) {
  return SQLITE_OK;
}

int Unlock(sqlite3_file*, int) {
  return SQLITE_OK;
}

int CheckReservedLock(sqlite3_file*, int* result) {
  *result = 0;
  return SQLITE_OK;
}

int FileControl(sqlite3_file*, int, void*) {
  return SQLITE_NOTFOUND;
}

int SectorSize(sqlite3_file*) {
  return 4096;
}

int DeviceCharacteristics(sqlite3_file*) {
  return 0;
}

const sqlite3_io_methods kIoMethods = {
    1,
    &Close,
    &Read,
    &Write,
    &Truncate,
    &Sync,
    &FileSize,
    &Lock,
    &Unlock,
    &CheckReservedLock,
    &FileControl,
    &SectorSize,
    &DeviceCharacteristics,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

class SqliteFileSystemVfs::Impl {
 public:
  // |file_system| is borrowed and must outlive this VFS.
  explicit Impl(io::FileSystem* file_system)
      : file_system_(file_system),
        name_("perfetto_file_system_" +
              std::to_string(g_next_vfs_id.fetch_add(1))) {
    parent_ = sqlite3_vfs_find(nullptr);
    vfs_.iVersion = 1;
    vfs_.szOsFile = static_cast<int>(sizeof(OpenFile));
    vfs_.mxPathname = parent_ ? parent_->mxPathname : 1024;
    vfs_.zName = name_.c_str();
    vfs_.pAppData = this;
    vfs_.xOpen = &Open;
    vfs_.xDelete = &Delete;
    vfs_.xAccess = &Access;
    vfs_.xFullPathname = &FullPathname;
    vfs_.xDlOpen = &DlOpen;
    vfs_.xDlError = &DlError;
    vfs_.xDlSym = &DlSym;
    vfs_.xDlClose = &DlClose;
    vfs_.xRandomness = &Randomness;
    vfs_.xSleep = &Sleep;
    vfs_.xCurrentTime = &CurrentTime;
    vfs_.xGetLastError = &GetLastError;
  }

  base::Status Register() {
    if (sqlite3_vfs_register(&vfs_, 0) != SQLITE_OK) {
      return base::ErrStatus("Failed to register SQLite file system VFS");
    }
    registered_ = true;
    return base::OkStatus();
  }

  ~Impl() {
    if (registered_) {
      sqlite3_vfs_unregister(&vfs_);
    }
  }

  const char* name() const { return name_.c_str(); }

 private:
  static Impl* From(sqlite3_vfs* vfs) {
    return static_cast<Impl*>(vfs->pAppData);
  }

  static int Open(sqlite3_vfs* vfs,
                  const char* path,
                  sqlite3_file* sqlite_file,
                  int flags,
                  int* out_flags) {
    Impl* self = From(vfs);
    std::string actual_path =
        path ? path
             : self->name_ + "_temp_" + std::to_string(self->next_temp_id_++);

    if ((flags & SQLITE_OPEN_EXCLUSIVE) && (flags & SQLITE_OPEN_CREATE)) {
      bool exists = false;
      if (!self->file_system_->FileExists(actual_path, &exists).ok() ||
          exists) {
        return SQLITE_CANTOPEN;
      }
    }

    io::FileOpenOptions options;
    if (flags & SQLITE_OPEN_READONLY) {
      options.access = io::FileAccess::kReadOnly;
    } else {
      options.access = io::FileAccess::kReadWrite;
    }
    options.create = (flags & SQLITE_OPEN_CREATE) != 0;

    std::unique_ptr<io::File> opened;
    if (!self->file_system_->OpenFile(actual_path, options, &opened).ok()) {
      return SQLITE_CANTOPEN;
    }

    auto* file = new (sqlite_file) OpenFile();
    file->file = std::move(opened);
    file->file_system = self->file_system_;
    file->path = std::move(actual_path);
    file->delete_on_close = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
    file->sqlite_file.pMethods = &kIoMethods;
    if (out_flags) {
      *out_flags = flags;
    }
    return SQLITE_OK;
  }

  static int Delete(sqlite3_vfs* vfs, const char* path, int) {
    if (!path) {
      return SQLITE_IOERR_DELETE;
    }
    return From(vfs)->file_system_->DeleteFile(path).ok() ? SQLITE_OK
                                                          : SQLITE_IOERR_DELETE;
  }

  static int Access(sqlite3_vfs* vfs, const char* path, int, int* result) {
    if (!path) {
      return SQLITE_IOERR_ACCESS;
    }
    bool exists = false;
    if (!From(vfs)->file_system_->FileExists(path, &exists).ok()) {
      return SQLITE_IOERR_ACCESS;
    }
    *result = exists ? 1 : 0;
    return SQLITE_OK;
  }

  static int FullPathname(sqlite3_vfs*,
                          const char* path,
                          int output_size,
                          char* output) {
    if (!path || output_size <= 0 ||
        strlen(path) >= static_cast<size_t>(output_size)) {
      return SQLITE_CANTOPEN;
    }
    memcpy(output, path, strlen(path) + 1);
    return SQLITE_OK;
  }

  static void* DlOpen(sqlite3_vfs* vfs, const char* path) {
    sqlite3_vfs* parent = From(vfs)->parent_;
    return parent && parent->xDlOpen ? parent->xDlOpen(parent, path) : nullptr;
  }

  static void DlError(sqlite3_vfs* vfs, int size, char* error) {
    sqlite3_vfs* parent = From(vfs)->parent_;
    if (parent && parent->xDlError) {
      parent->xDlError(parent, size, error);
    } else if (size > 0) {
      error[0] = '\0';
    }
  }

  static void (*DlSym(sqlite3_vfs* vfs, void* handle, const char* symbol))() {
    sqlite3_vfs* parent = From(vfs)->parent_;
    return parent && parent->xDlSym ? parent->xDlSym(parent, handle, symbol)
                                    : nullptr;
  }

  static void DlClose(sqlite3_vfs* vfs, void* handle) {
    sqlite3_vfs* parent = From(vfs)->parent_;
    if (parent && parent->xDlClose) {
      parent->xDlClose(parent, handle);
    }
  }

  static int Randomness(sqlite3_vfs* vfs, int size, char* output) {
    sqlite3_vfs* parent = From(vfs)->parent_;
    if (parent && parent->xRandomness) {
      return parent->xRandomness(parent, size, output);
    }
    memset(output, 0, static_cast<size_t>(size));
    return size;
  }

  static int Sleep(sqlite3_vfs* vfs, int microseconds) {
    sqlite3_vfs* parent = From(vfs)->parent_;
    return parent && parent->xSleep ? parent->xSleep(parent, microseconds)
                                    : microseconds;
  }

  static int CurrentTime(sqlite3_vfs* vfs, double* time) {
    sqlite3_vfs* parent = From(vfs)->parent_;
    return parent && parent->xCurrentTime ? parent->xCurrentTime(parent, time)
                                          : SQLITE_ERROR;
  }

  static int GetLastError(sqlite3_vfs* vfs, int size, char* error) {
    sqlite3_vfs* parent = From(vfs)->parent_;
    return parent && parent->xGetLastError
               ? parent->xGetLastError(parent, size, error)
               : 0;
  }

  // Borrowed; must outlive the VFS (see io.h platform contract).
  io::FileSystem* file_system_ = nullptr;
  std::string name_;
  sqlite3_vfs* parent_ = nullptr;
  sqlite3_vfs vfs_{};
  uint64_t next_temp_id_ = 0;
  bool registered_ = false;
};

base::StatusOr<std::unique_ptr<SqliteFileSystemVfs>>
SqliteFileSystemVfs::Create(io::FileSystem* file_system) {
  if (!file_system) {
    return base::ErrStatus("SQLite file system is null");
  }
  auto impl = std::make_unique<Impl>(file_system);
  RETURN_IF_ERROR(impl->Register());
  return std::unique_ptr<SqliteFileSystemVfs>(
      new SqliteFileSystemVfs(std::move(impl)));
}

SqliteFileSystemVfs::SqliteFileSystemVfs(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
SqliteFileSystemVfs::~SqliteFileSystemVfs() = default;

const char* SqliteFileSystemVfs::name() const {
  return impl_->name();
}

}  // namespace perfetto::trace_processor
