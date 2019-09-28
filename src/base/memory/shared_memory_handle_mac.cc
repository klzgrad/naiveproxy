// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_handle.h"

#include <mach/mach_vm.h>
#include <stddef.h>

#include "base/mac/mac_util.h"
#include "base/mac/mach_logging.h"
#include "base/unguessable_token.h"

namespace base {

SharedMemoryHandle::SharedMemoryHandle() {}

SharedMemoryHandle::SharedMemoryHandle(mach_vm_size_t size,
                                       const base::UnguessableToken& guid) {
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
    : memory_object_(memory_object),
      ownership_passes_to_ipc_(false),
      guid_(guid),
      size_(size) {}

SharedMemoryHandle SharedMemoryHandle::Duplicate() const {
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

bool SharedMemoryHandle::IsValid() const {
  return memory_object_ != MACH_PORT_NULL;
}

mach_port_t SharedMemoryHandle::GetMemoryObject() const {
  return memory_object_;
}

bool SharedMemoryHandle::MapAt(off_t offset,
                               size_t bytes,
                               void** memory,
                               bool read_only) {
  DCHECK(IsValid());
  kern_return_t kr = mach_vm_map(
      mach_task_self(),
      reinterpret_cast<mach_vm_address_t*>(memory),  // Output parameter
      bytes,
      0,  // Alignment mask
      VM_FLAGS_ANYWHERE, memory_object_, offset,
      FALSE,                                           // Copy
      VM_PROT_READ | (read_only ? 0 : VM_PROT_WRITE),  // Current protection
      VM_PROT_WRITE | VM_PROT_READ | VM_PROT_IS_MASK,  // Maximum protection
      VM_INHERIT_NONE);
  return kr == KERN_SUCCESS;
}

void SharedMemoryHandle::Close() const {
  if (!IsValid())
    return;

  kern_return_t kr = mach_port_deallocate(mach_task_self(), memory_object_);
  MACH_DLOG_IF(ERROR, kr != KERN_SUCCESS, kr) << "Error deallocating mach port";
}

void SharedMemoryHandle::SetOwnershipPassesToIPC(bool ownership_passes) {
  ownership_passes_to_ipc_ = ownership_passes;
}

bool SharedMemoryHandle::OwnershipPassesToIPC() const {
  return ownership_passes_to_ipc_;
}

}  // namespace base
