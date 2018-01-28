// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/blockfile/file.h"

#include <limits.h>
#include <utility>

#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/message_loop/message_loop.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/disk_cache.h"

namespace {

// Structure used for asynchronous operations.
struct MyOverlapped {
  MyOverlapped(disk_cache::File* file, size_t offset,
               disk_cache::FileIOCallback* callback);
  ~MyOverlapped() {}
  OVERLAPPED* overlapped() {
    return &context_.overlapped;
  }

  base::MessageLoopForIO::IOContext context_;
  scoped_refptr<disk_cache::File> file_;
  disk_cache::FileIOCallback* callback_;
};

static_assert(offsetof(MyOverlapped, context_) == 0,
              "should start with overlapped");

// Helper class to handle the IO completion notifications from the message loop.
class CompletionHandler : public base::MessageLoopForIO::IOHandler {
  void OnIOCompleted(base::MessageLoopForIO::IOContext* context,
                     DWORD actual_bytes,
                     DWORD error) override;
};

static base::LazyInstance<CompletionHandler>::DestructorAtExit
    g_completion_handler = LAZY_INSTANCE_INITIALIZER;

void CompletionHandler::OnIOCompleted(
    base::MessageLoopForIO::IOContext* context,
    DWORD actual_bytes,
    DWORD error) {
  MyOverlapped* data = reinterpret_cast<MyOverlapped*>(context);

  if (error) {
    DCHECK(!actual_bytes);
    actual_bytes = static_cast<DWORD>(net::ERR_CACHE_READ_FAILURE);
    NOTREACHED();
  }

  if (data->callback_)
    data->callback_->OnFileIOComplete(static_cast<int>(actual_bytes));

  delete data;
}

MyOverlapped::MyOverlapped(disk_cache::File* file, size_t offset,
                           disk_cache::FileIOCallback* callback) {
  context_.overlapped.Offset = static_cast<DWORD>(offset);
  file_ = file;
  callback_ = callback;
}

}  // namespace

namespace disk_cache {

File::File(base::File file)
    : init_(true), mixed_(true), sync_base_file_(std::move(file)) {}

bool File::Init(const base::FilePath& name) {
  DCHECK(!init_);
  if (init_)
    return false;

  DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  DWORD access = GENERIC_READ | GENERIC_WRITE | DELETE;
  base_file_ =
      base::File(CreateFile(name.value().c_str(), access, sharing, NULL,
                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL));

  if (!base_file_.IsValid())
    return false;

  base::MessageLoopForIO::current()->RegisterIOHandler(
      base_file_.GetPlatformFile(), g_completion_handler.Pointer());

  init_ = true;
  sync_base_file_  =
    base::File(CreateFile(name.value().c_str(), access, sharing, NULL,
                          OPEN_EXISTING, 0, NULL));

  if (!sync_base_file_.IsValid())
    return false;

  return true;
}

bool File::IsValid() const {
  if (!init_)
    return false;
  return base_file_.IsValid() || sync_base_file_.IsValid();
}

bool File::Read(void* buffer, size_t buffer_len, size_t offset) {
  DCHECK(init_);
  if (buffer_len > ULONG_MAX || offset > LONG_MAX)
    return false;

  int ret = sync_base_file_.Read(offset, static_cast<char*>(buffer),
                                 buffer_len);
  return static_cast<int>(buffer_len) == ret;
}

bool File::Write(const void* buffer, size_t buffer_len, size_t offset) {
  DCHECK(init_);
  if (buffer_len > ULONG_MAX || offset > ULONG_MAX)
    return false;

  int ret = sync_base_file_.Write(offset, static_cast<const char*>(buffer),
                                 buffer_len);
  return static_cast<int>(buffer_len) == ret;
}

// We have to increase the ref counter of the file before performing the IO to
// prevent the completion to happen with an invalid handle (if the file is
// closed while the IO is in flight).
bool File::Read(void* buffer, size_t buffer_len, size_t offset,
                FileIOCallback* callback, bool* completed) {
  DCHECK(init_);
  if (!callback) {
    if (completed)
      *completed = true;
    return Read(buffer, buffer_len, offset);
  }

  if (buffer_len > ULONG_MAX || offset > ULONG_MAX)
    return false;

  MyOverlapped* data = new MyOverlapped(this, offset, callback);
  DWORD size = static_cast<DWORD>(buffer_len);

  DWORD actual;
  if (!ReadFile(base_file_.GetPlatformFile(), buffer, size, &actual,
                data->overlapped())) {
    *completed = false;
    if (GetLastError() == ERROR_IO_PENDING)
      return true;
    delete data;
    return false;
  }

  // The operation completed already. We'll be called back anyway.
  *completed = (actual == size);
  DCHECK_EQ(size, actual);
  data->callback_ = NULL;
  data->file_ = NULL;  // There is no reason to hold on to this anymore.
  return *completed;
}

bool File::Write(const void* buffer, size_t buffer_len, size_t offset,
                 FileIOCallback* callback, bool* completed) {
  DCHECK(init_);
  if (!callback) {
    if (completed)
      *completed = true;
    return Write(buffer, buffer_len, offset);
  }

  return AsyncWrite(buffer, buffer_len, offset, callback, completed);
}

File::~File() {
}

base::PlatformFile File::platform_file() const {
  DCHECK(init_);
  return base_file_.IsValid() ? base_file_.GetPlatformFile() :
                                sync_base_file_.GetPlatformFile();
}

bool File::AsyncWrite(const void* buffer, size_t buffer_len, size_t offset,
                      FileIOCallback* callback, bool* completed) {
  DCHECK(init_);
  DCHECK(callback);
  DCHECK(completed);
  if (buffer_len > ULONG_MAX || offset > ULONG_MAX)
    return false;

  MyOverlapped* data = new MyOverlapped(this, offset, callback);
  DWORD size = static_cast<DWORD>(buffer_len);

  DWORD actual;
  if (!WriteFile(base_file_.GetPlatformFile(), buffer, size, &actual,
                 data->overlapped())) {
    *completed = false;
    if (GetLastError() == ERROR_IO_PENDING)
      return true;
    delete data;
    return false;
  }

  // The operation completed already. We'll be called back anyway.
  *completed = (actual == size);
  DCHECK_EQ(size, actual);
  data->callback_ = NULL;
  data->file_ = NULL;  // There is no reason to hold on to this anymore.
  return *completed;
}

bool File::SetLength(size_t length) {
  DCHECK(init_);
  if (length > ULONG_MAX)
    return false;

  DWORD size = static_cast<DWORD>(length);
  HANDLE file = platform_file();
  if (INVALID_SET_FILE_POINTER == SetFilePointer(file, size, NULL, FILE_BEGIN))
    return false;

  return TRUE == SetEndOfFile(file);
}

size_t File::GetLength() {
  DCHECK(init_);
  LARGE_INTEGER size;
  HANDLE file = platform_file();
  if (!GetFileSizeEx(file, &size))
    return 0;
  if (size.HighPart)
    return ULONG_MAX;

  return static_cast<size_t>(size.LowPart);
}

// Static.
void File::WaitForPendingIO(int* num_pending_io) {
  while (*num_pending_io) {
    // Asynchronous IO operations may be in flight and the completion may end
    // up calling us back so let's wait for them.
    base::MessageLoopForIO::IOHandler* handler = g_completion_handler.Pointer();
    base::MessageLoopForIO::current()->WaitForIOCompletion(100, handler);
  }
}

// Static.
void File::DropPendingIO() {
}

}  // namespace disk_cache
