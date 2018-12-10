// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_handle.h"

#include <sys/mman.h>
#include <unistd.h>

#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/posix/unix_domain_socket.h"
#include "base/unguessable_token.h"
#include "third_party/ashmem/ashmem.h"

namespace base {

static int GetAshmemRegionProtectionMask(int fd) {
  int prot = ashmem_get_prot_region(fd);
  if (prot < 0) {
    DPLOG(ERROR) << "ashmem_get_prot_region";
    return -1;
  }
  return prot;
}

SharedMemoryHandle::SharedMemoryHandle() = default;

SharedMemoryHandle::SharedMemoryHandle(
    const base::FileDescriptor& file_descriptor,
    size_t size,
    const base::UnguessableToken& guid)
    : guid_(guid), size_(size) {
  DCHECK_GE(file_descriptor.fd, 0);
  file_descriptor_ = file_descriptor;
}

// static
SharedMemoryHandle SharedMemoryHandle::ImportHandle(int fd, size_t size) {
  SharedMemoryHandle handle;
  handle.file_descriptor_.fd = fd;
  handle.file_descriptor_.auto_close = false;
  handle.guid_ = UnguessableToken::Create();
  handle.size_ = size;
  return handle;
}

int SharedMemoryHandle::GetHandle() const {
  DCHECK(IsValid());
  return file_descriptor_.fd;
}

bool SharedMemoryHandle::IsValid() const {
  return file_descriptor_.fd >= 0;
}

void SharedMemoryHandle::Close() const {
  DCHECK(IsValid());
  if (IGNORE_EINTR(close(file_descriptor_.fd)) < 0)
    PLOG(ERROR) << "close";
}

int SharedMemoryHandle::Release() {
  int old_fd = file_descriptor_.fd;
  file_descriptor_.fd = -1;
  return old_fd;
}

SharedMemoryHandle SharedMemoryHandle::Duplicate() const {
  DCHECK(IsValid());
  SharedMemoryHandle result;
  int duped_handle = HANDLE_EINTR(dup(file_descriptor_.fd));
  if (duped_handle >= 0) {
    result = SharedMemoryHandle(FileDescriptor(duped_handle, true), GetSize(),
                                GetGUID());
    if (IsReadOnly())
      result.SetReadOnly();
  }
  return result;
}

void SharedMemoryHandle::SetOwnershipPassesToIPC(bool ownership_passes) {
  file_descriptor_.auto_close = ownership_passes;
}

bool SharedMemoryHandle::OwnershipPassesToIPC() const {
  return file_descriptor_.auto_close;
}

bool SharedMemoryHandle::IsRegionReadOnly() const {
  int prot = GetAshmemRegionProtectionMask(file_descriptor_.fd);
  return (prot >= 0 && (prot & PROT_WRITE) == 0);
}

bool SharedMemoryHandle::SetRegionReadOnly() const {
  int fd = file_descriptor_.fd;
  int prot = GetAshmemRegionProtectionMask(fd);
  if (prot < 0)
    return false;

  if ((prot & PROT_WRITE) == 0) {
    // Region is already read-only.
    return true;
  }

  prot &= ~PROT_WRITE;
  int ret = ashmem_set_prot_region(fd, prot);
  if (ret != 0) {
    DPLOG(ERROR) << "ashmem_set_prot_region";
    return false;
  }
  return true;
}

}  // namespace base
