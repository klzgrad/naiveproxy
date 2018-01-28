// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/blockfile/file.h"

#include <stdint.h>
#include <limits>
#include <utility>

#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/task_runner_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/disk_cache.h"

namespace {

// The maximum number of threads for this pool.
const int kMaxThreads = 5;

class FileWorkerPool : public base::SequencedWorkerPool {
 public:
  FileWorkerPool()
      : base::SequencedWorkerPool(kMaxThreads,
                                  "CachePool",
                                  base::TaskPriority::USER_BLOCKING) {}

 protected:
  ~FileWorkerPool() override {}
};

base::LazyInstance<FileWorkerPool>::Leaky s_worker_pool =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

namespace disk_cache {

File::File(base::File file)
    : init_(true), mixed_(true), base_file_(std::move(file)) {}

bool File::Init(const base::FilePath& name) {
  if (base_file_.IsValid())
    return false;

  int flags = base::File::FLAG_OPEN | base::File::FLAG_READ |
              base::File::FLAG_WRITE;
  base_file_.Initialize(name, flags);
  return base_file_.IsValid();
}

bool File::IsValid() const {
  return base_file_.IsValid();
}

bool File::Read(void* buffer, size_t buffer_len, size_t offset) {
  DCHECK(base_file_.IsValid());
  if (buffer_len > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
      offset > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    return false;
  }

  int ret = base_file_.Read(offset, static_cast<char*>(buffer), buffer_len);
  return (static_cast<size_t>(ret) == buffer_len);
}

bool File::Write(const void* buffer, size_t buffer_len, size_t offset) {
  DCHECK(base_file_.IsValid());
  if (buffer_len > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
      offset > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    return false;
  }

  int ret = base_file_.Write(offset, static_cast<const char*>(buffer),
                             buffer_len);
  return (static_cast<size_t>(ret) == buffer_len);
}

bool File::Read(void* buffer, size_t buffer_len, size_t offset,
                FileIOCallback* callback, bool* completed) {
  DCHECK(base_file_.IsValid());
  if (!callback) {
    if (completed)
      *completed = true;
    return Read(buffer, buffer_len, offset);
  }

  if (buffer_len > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
      offset > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    return false;
  }

  base::PostTaskAndReplyWithResult(
      s_worker_pool.Pointer(), FROM_HERE,
      base::Bind(&File::DoRead, base::Unretained(this), buffer, buffer_len,
                 offset),
      base::Bind(&File::OnOperationComplete, this, callback));

  *completed = false;
  return true;
}

bool File::Write(const void* buffer, size_t buffer_len, size_t offset,
                 FileIOCallback* callback, bool* completed) {
  DCHECK(base_file_.IsValid());
  if (!callback) {
    if (completed)
      *completed = true;
    return Write(buffer, buffer_len, offset);
  }

  if (buffer_len > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
      offset > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    return false;
  }

  base::PostTaskAndReplyWithResult(
      s_worker_pool.Pointer(), FROM_HERE,
      base::Bind(&File::DoWrite, base::Unretained(this), buffer, buffer_len,
                 offset),
      base::Bind(&File::OnOperationComplete, this, callback));

  *completed = false;
  return true;
}

bool File::SetLength(size_t length) {
  DCHECK(base_file_.IsValid());
  if (length > std::numeric_limits<uint32_t>::max())
    return false;

  return base_file_.SetLength(length);
}

size_t File::GetLength() {
  DCHECK(base_file_.IsValid());
  int64_t len = base_file_.GetLength();

  if (len < 0)
    return 0;
  if (len > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
    return std::numeric_limits<uint32_t>::max();

  return static_cast<size_t>(len);
}

// Static.
void File::WaitForPendingIO(int* num_pending_io) {
  // We are running unit tests so we should wait for all callbacks. Sadly, the
  // worker pool only waits for tasks on the worker pool, not the "Reply" tasks
  // so we have to let the current message loop to run.
  s_worker_pool.Get().FlushForTesting();
  base::RunLoop().RunUntilIdle();
}

// Static.
void File::DropPendingIO() {
}


File::~File() {
}

base::PlatformFile File::platform_file() const {
  return base_file_.GetPlatformFile();
}

// Runs on a worker thread.
int File::DoRead(void* buffer, size_t buffer_len, size_t offset) {
  if (Read(const_cast<void*>(buffer), buffer_len, offset))
    return static_cast<int>(buffer_len);

  return net::ERR_CACHE_READ_FAILURE;
}

// Runs on a worker thread.
int File::DoWrite(const void* buffer, size_t buffer_len, size_t offset) {
  if (Write(const_cast<void*>(buffer), buffer_len, offset))
    return static_cast<int>(buffer_len);

  return net::ERR_CACHE_WRITE_FAILURE;
}

// This method actually makes sure that the last reference to the file doesn't
// go away on the worker pool.
void File::OnOperationComplete(FileIOCallback* callback, int result) {
  callback->OnFileIOComplete(result);
}

}  // namespace disk_cache
