// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_handle.h"

#include <mach/mach_vm.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

#include "base/mac/mac_util.h"
#include "base/mac/mach_logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/unguessable_token.h"

namespace base {

SharedMemoryHandle::SharedMemoryHandle() {}

SharedMemoryHandle::SharedMemoryHandle(
    const base::FileDescriptor& file_descriptor,
    size_t size,
    const base::UnguessableToken& guid)
    : type_(POSIX),
      file_descriptor_(file_descriptor),
      guid_(guid),
      size_(size) {}

SharedMemoryHandle::SharedMemoryHandle(mach_vm_size_t size,
                                       const base::UnguessableToken& guid) {
  type_ = MACH;
  mach_port_t named_right;
  kern_return_t kr = mach_make_memory_entry_64(
      mach_task_self(),
      &size,
      0,  // Address.
      MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE,
      &named_right,
      MACH_PORT_NULL);  // Parent handle.
  if (kr != KERN_SUCCESS) {
    memory_object_ = MACH_PORT_NULL;
    return;
  }

  memory_object_ = named_right;
  size_ = size;
  ownership_passes_to_ipc_ = false;
  guid_ = guid;
}

SharedMemoryHandle::SharedMemoryHandle(mach_port_t memory_object,
                                       mach_vm_size_t size,
                                       const base::UnguessableToken& guid)
    : type_(MACH),
      memory_object_(memory_object),
      ownership_passes_to_ipc_(false),
      guid_(guid),
      size_(size) {}

SharedMemoryHandle SharedMemoryHandle::Duplicate() const {
  switch (type_) {
    case POSIX: {
      if (!IsValid())
        return SharedMemoryHandle();
      int duped_fd = HANDLE_EINTR(dup(file_descriptor_.fd));
      if (duped_fd < 0)
        return SharedMemoryHandle();
      return SharedMemoryHandle(FileDescriptor(duped_fd, true), size_, guid_);
    }
    case MACH: {
      if (!IsValid())
        return SharedMemoryHandle();

      // Increment the ref count.
      kern_return_t kr = mach_port_mod_refs(mach_task_self(), memory_object_,
                                            MACH_PORT_RIGHT_SEND, 1);
      DCHECK_EQ(kr, KERN_SUCCESS);
      SharedMemoryHandle handle(*this);
      handle.SetOwnershipPassesToIPC(true);
      return handle;
    }
  }
}

bool SharedMemoryHandle::IsValid() const {
  switch (type_) {
    case POSIX:
      return file_descriptor_.fd >= 0;
    case MACH:
      return memory_object_ != MACH_PORT_NULL;
  }
}

mach_port_t SharedMemoryHandle::GetMemoryObject() const {
  DCHECK_EQ(type_, MACH);
  return memory_object_;
}

bool SharedMemoryHandle::MapAt(off_t offset,
                               size_t bytes,
                               void** memory,
                               bool read_only) {
  DCHECK(IsValid());
  switch (type_) {
    case SharedMemoryHandle::POSIX:
      *memory = mmap(nullptr, bytes, PROT_READ | (read_only ? 0 : PROT_WRITE),
                     MAP_SHARED, file_descriptor_.fd, offset);
      return *memory != MAP_FAILED;
    case SharedMemoryHandle::MACH:
      kern_return_t kr = mach_vm_map(
          mach_task_self(),
          reinterpret_cast<mach_vm_address_t*>(memory),    // Output parameter
          bytes,
          0,                                               // Alignment mask
          VM_FLAGS_ANYWHERE,
          memory_object_,
          offset,
          FALSE,                                           // Copy
          VM_PROT_READ | (read_only ? 0 : VM_PROT_WRITE),  // Current protection
          VM_PROT_WRITE | VM_PROT_READ | VM_PROT_IS_MASK,  // Maximum protection
          VM_INHERIT_NONE);
      return kr == KERN_SUCCESS;
  }
}

void SharedMemoryHandle::Close() const {
  if (!IsValid())
    return;

  switch (type_) {
    case POSIX:
      if (IGNORE_EINTR(close(file_descriptor_.fd)) < 0)
        DPLOG(ERROR) << "Error closing fd";
      break;
    case MACH:
      kern_return_t kr = mach_port_deallocate(mach_task_self(), memory_object_);
      if (kr != KERN_SUCCESS)
        MACH_DLOG(ERROR, kr) << "Error deallocating mach port";
      break;
  }
}

void SharedMemoryHandle::SetOwnershipPassesToIPC(bool ownership_passes) {
  DCHECK_EQ(type_, MACH);
  ownership_passes_to_ipc_ = ownership_passes;
}

bool SharedMemoryHandle::OwnershipPassesToIPC() const {
  DCHECK_EQ(type_, MACH);
  return ownership_passes_to_ipc_;
}

}  // namespace base
