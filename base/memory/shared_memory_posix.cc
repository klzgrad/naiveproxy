// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/memory/shared_memory_helper.h"
#include "base/memory/shared_memory_tracker.h"
#include "base/posix/eintr_wrapper.h"
#include "base/posix/safe_strerror.h"
#include "base/process/process_metrics.h"
#include "base/scoped_generic.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/trace_event/trace_event.h"
#include "base/unguessable_token.h"
#include "build/build_config.h"

#if defined(OS_ANDROID)
#include "base/os_compat_android.h"
#include "third_party/ashmem/ashmem.h"
#endif

namespace base {

SharedMemory::SharedMemory() {}

SharedMemory::SharedMemory(const SharedMemoryHandle& handle, bool read_only)
    : shm_(handle), read_only_(read_only) {}

SharedMemory::~SharedMemory() {
  Unmap();
  Close();
}

// static
bool SharedMemory::IsHandleValid(const SharedMemoryHandle& handle) {
  return handle.IsValid();
}

// static
void SharedMemory::CloseHandle(const SharedMemoryHandle& handle) {
  DCHECK(handle.IsValid());
  handle.Close();
}

// static
size_t SharedMemory::GetHandleLimit() {
  return base::GetMaxFds();
}

// static
SharedMemoryHandle SharedMemory::DuplicateHandle(
    const SharedMemoryHandle& handle) {
  return handle.Duplicate();
}

// static
int SharedMemory::GetFdFromSharedMemoryHandle(
    const SharedMemoryHandle& handle) {
  return handle.GetHandle();
}

bool SharedMemory::CreateAndMapAnonymous(size_t size) {
  return CreateAnonymous(size) && Map(size);
}

#if !defined(OS_ANDROID)
// Chromium mostly only uses the unique/private shmem as specified by
// "name == L"". The exception is in the StatsTable.
// TODO(jrg): there is no way to "clean up" all unused named shmem if
// we restart from a crash.  (That isn't a new problem, but it is a problem.)
// In case we want to delete it later, it may be useful to save the value
// of mem_filename after FilePathForMemoryName().
bool SharedMemory::Create(const SharedMemoryCreateOptions& options) {
  DCHECK(!shm_.IsValid());
  if (options.size == 0) return false;

  if (options.size > static_cast<size_t>(std::numeric_limits<int>::max()))
    return false;

  // This function theoretically can block on the disk, but realistically
  // the temporary files we create will just go into the buffer cache
  // and be deleted before they ever make it out to disk.
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  ScopedFILE fp;
  bool fix_size = true;
  ScopedFD readonly_fd;

  FilePath path;
  if (options.name_deprecated == NULL || options.name_deprecated->empty()) {
    bool result =
        CreateAnonymousSharedMemory(options, &fp, &readonly_fd, &path);
    if (!result)
      return false;
  } else {
    if (!FilePathForMemoryName(*options.name_deprecated, &path))
      return false;

    // Make sure that the file is opened without any permission
    // to other users on the system.
    const mode_t kOwnerOnly = S_IRUSR | S_IWUSR;

    // First, try to create the file.
    int fd = HANDLE_EINTR(
        open(path.value().c_str(), O_RDWR | O_CREAT | O_EXCL, kOwnerOnly));
    if (fd == -1 && options.open_existing_deprecated) {
      // If this doesn't work, try and open an existing file in append mode.
      // Opening an existing file in a world writable directory has two main
      // security implications:
      // - Attackers could plant a file under their control, so ownership of
      //   the file is checked below.
      // - Attackers could plant a symbolic link so that an unexpected file
      //   is opened, so O_NOFOLLOW is passed to open().
#if !defined(OS_AIX)
      fd = HANDLE_EINTR(
          open(path.value().c_str(), O_RDWR | O_APPEND | O_NOFOLLOW));
#else
      // AIX has no 64-bit support for open flags such as -
      //  O_CLOEXEC, O_NOFOLLOW and O_TTY_INIT.
      fd = HANDLE_EINTR(open(path.value().c_str(), O_RDWR | O_APPEND));
#endif
      // Check that the current user owns the file.
      // If uid != euid, then a more complex permission model is used and this
      // API is not appropriate.
      const uid_t real_uid = getuid();
      const uid_t effective_uid = geteuid();
      struct stat sb;
      if (fd >= 0 &&
          (fstat(fd, &sb) != 0 || sb.st_uid != real_uid ||
           sb.st_uid != effective_uid)) {
        LOG(ERROR) <<
            "Invalid owner when opening existing shared memory file.";
        close(fd);
        return false;
      }

      // An existing file was opened, so its size should not be fixed.
      fix_size = false;
    }

    if (options.share_read_only) {
      // Also open as readonly so that we can GetReadOnlyHandle.
      readonly_fd.reset(HANDLE_EINTR(open(path.value().c_str(), O_RDONLY)));
      if (!readonly_fd.is_valid()) {
        DPLOG(ERROR) << "open(\"" << path.value() << "\", O_RDONLY) failed";
        close(fd);
        fd = -1;
        return false;
      }
    }
    if (fd >= 0) {
      // "a+" is always appropriate: if it's a new file, a+ is similar to w+.
      fp.reset(fdopen(fd, "a+"));
    }
  }
  if (fp && fix_size) {
    // Get current size.
    struct stat stat;
    if (fstat(fileno(fp.get()), &stat) != 0)
      return false;
    const size_t current_size = stat.st_size;
    if (current_size != options.size) {
      if (HANDLE_EINTR(ftruncate(fileno(fp.get()), options.size)) != 0)
        return false;
    }
    requested_size_ = options.size;
  }
  if (fp == NULL) {
    PLOG(ERROR) << "Creating shared memory in " << path.value() << " failed";
    FilePath dir = path.DirName();
    if (access(dir.value().c_str(), W_OK | X_OK) < 0) {
      PLOG(ERROR) << "Unable to access(W_OK|X_OK) " << dir.value();
      if (dir.value() == "/dev/shm") {
        LOG(FATAL) << "This is frequently caused by incorrect permissions on "
                   << "/dev/shm.  Try 'sudo chmod 1777 /dev/shm' to fix.";
      }
    }
    return false;
  }

  int mapped_file = -1;
  int readonly_mapped_file = -1;
  bool result = PrepareMapFile(std::move(fp), std::move(readonly_fd),
                               &mapped_file, &readonly_mapped_file);
  shm_ = SharedMemoryHandle(base::FileDescriptor(mapped_file, false),
                            options.size, UnguessableToken::Create());
  readonly_shm_ =
      SharedMemoryHandle(base::FileDescriptor(readonly_mapped_file, false),
                         options.size, shm_.GetGUID());
  return result;
}

// Our current implementation of shmem is with mmap()ing of files.
// These files need to be deleted explicitly.
// In practice this call is only needed for unit tests.
bool SharedMemory::Delete(const std::string& name) {
  FilePath path;
  if (!FilePathForMemoryName(name, &path))
    return false;

  if (PathExists(path))
    return base::DeleteFile(path, false);

  // Doesn't exist, so success.
  return true;
}

bool SharedMemory::Open(const std::string& name, bool read_only) {
  FilePath path;
  if (!FilePathForMemoryName(name, &path))
    return false;

  read_only_ = read_only;

  const char *mode = read_only ? "r" : "r+";
  ScopedFILE fp(base::OpenFile(path, mode));
  ScopedFD readonly_fd(HANDLE_EINTR(open(path.value().c_str(), O_RDONLY)));
  if (!readonly_fd.is_valid()) {
    DPLOG(ERROR) << "open(\"" << path.value() << "\", O_RDONLY) failed";
    return false;
  }
  int mapped_file = -1;
  int readonly_mapped_file = -1;
  bool result = PrepareMapFile(std::move(fp), std::move(readonly_fd),
                               &mapped_file, &readonly_mapped_file);
  // This form of sharing shared memory is deprecated. https://crbug.com/345734.
  // However, we can't get rid of it without a significant refactor because its
  // used to communicate between two versions of the same service process, very
  // early in the life cycle.
  // Technically, we should also pass the GUID from the original shared memory
  // region. We don't do that - this means that we will overcount this memory,
  // which thankfully isn't relevant since Chrome only communicates with a
  // single version of the service process.
  // We pass the size |0|, which is a dummy size and wrong, but otherwise
  // harmless.
  shm_ = SharedMemoryHandle(base::FileDescriptor(mapped_file, false), 0u,
                            UnguessableToken::Create());
  readonly_shm_ = SharedMemoryHandle(
      base::FileDescriptor(readonly_mapped_file, false), 0, shm_.GetGUID());
  return result;
}
#endif  // !defined(OS_ANDROID)

bool SharedMemory::MapAt(off_t offset, size_t bytes) {
  if (!shm_.IsValid())
    return false;

  if (bytes > static_cast<size_t>(std::numeric_limits<int>::max()))
    return false;

  if (memory_)
    return false;

#if defined(OS_ANDROID)
  // On Android, Map can be called with a size and offset of zero to use the
  // ashmem-determined size.
  if (bytes == 0) {
    DCHECK_EQ(0, offset);
    int ashmem_bytes = ashmem_get_size_region(shm_.GetHandle());
    if (ashmem_bytes < 0)
      return false;
    bytes = ashmem_bytes;
  }
#endif

  memory_ = mmap(NULL, bytes, PROT_READ | (read_only_ ? 0 : PROT_WRITE),
                 MAP_SHARED, shm_.GetHandle(), offset);

  bool mmap_succeeded = memory_ != (void*)-1 && memory_ != NULL;
  if (mmap_succeeded) {
    mapped_size_ = bytes;
    mapped_id_ = shm_.GetGUID();
    DCHECK_EQ(0U,
              reinterpret_cast<uintptr_t>(memory_) &
                  (SharedMemory::MAP_MINIMUM_ALIGNMENT - 1));
    SharedMemoryTracker::GetInstance()->IncrementMemoryUsage(*this);
  } else {
    memory_ = NULL;
  }

  return mmap_succeeded;
}

bool SharedMemory::Unmap() {
  if (memory_ == NULL)
    return false;

  SharedMemoryTracker::GetInstance()->DecrementMemoryUsage(*this);
  munmap(memory_, mapped_size_);
  memory_ = NULL;
  mapped_size_ = 0;
  mapped_id_ = UnguessableToken();
  return true;
}

SharedMemoryHandle SharedMemory::handle() const {
  return shm_;
}

SharedMemoryHandle SharedMemory::TakeHandle() {
  SharedMemoryHandle handle_copy = shm_;
  handle_copy.SetOwnershipPassesToIPC(true);
  shm_ = SharedMemoryHandle();
  memory_ = nullptr;
  mapped_size_ = 0;
  return handle_copy;
}

void SharedMemory::Close() {
  if (shm_.IsValid()) {
    shm_.Close();
    shm_ = SharedMemoryHandle();
  }
  if (readonly_shm_.IsValid()) {
    readonly_shm_.Close();
    readonly_shm_ = SharedMemoryHandle();
  }
}

#if !defined(OS_ANDROID)
// For the given shmem named |mem_name|, return a filename to mmap()
// (and possibly create).  Modifies |filename|.  Return false on
// error, or true of we are happy.
bool SharedMemory::FilePathForMemoryName(const std::string& mem_name,
                                         FilePath* path) {
  // mem_name will be used for a filename; make sure it doesn't
  // contain anything which will confuse us.
  DCHECK_EQ(std::string::npos, mem_name.find('/'));
  DCHECK_EQ(std::string::npos, mem_name.find('\0'));

  FilePath temp_dir;
  if (!GetShmemTempDir(false, &temp_dir))
    return false;

#if defined(GOOGLE_CHROME_BUILD)
  std::string name_base = std::string("com.google.Chrome");
#else
  std::string name_base = std::string("org.chromium.Chromium");
#endif
  *path = temp_dir.AppendASCII(name_base + ".shmem." + mem_name);
  return true;
}
#endif  // !defined(OS_ANDROID)

SharedMemoryHandle SharedMemory::GetReadOnlyHandle() {
  CHECK(readonly_shm_.IsValid());
  return readonly_shm_.Duplicate();
}

}  // namespace base
