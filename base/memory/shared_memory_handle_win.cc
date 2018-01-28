// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_handle.h"

#include "base/logging.h"
#include "base/unguessable_token.h"

namespace base {

SharedMemoryHandle::SharedMemoryHandle() {}

SharedMemoryHandle::SharedMemoryHandle(HANDLE h,
                                       size_t size,
                                       const base::UnguessableToken& guid)
    : handle_(h), guid_(guid), size_(size) {}

void SharedMemoryHandle::Close() const {
  DCHECK(handle_ != nullptr);
  ::CloseHandle(handle_);
}

bool SharedMemoryHandle::IsValid() const {
  return handle_ != nullptr;
}

SharedMemoryHandle SharedMemoryHandle::Duplicate() const {
  HANDLE duped_handle;
  ProcessHandle process = GetCurrentProcess();
  BOOL success = ::DuplicateHandle(process, handle_, process, &duped_handle, 0,
                                   FALSE, DUPLICATE_SAME_ACCESS);
  if (!success)
    return SharedMemoryHandle();

  base::SharedMemoryHandle handle(duped_handle, GetSize(), GetGUID());
  handle.SetOwnershipPassesToIPC(true);
  return handle;
}

HANDLE SharedMemoryHandle::GetHandle() const {
  return handle_;
}

void SharedMemoryHandle::SetOwnershipPassesToIPC(bool ownership_passes) {
  ownership_passes_to_ipc_ = ownership_passes;
}

bool SharedMemoryHandle::OwnershipPassesToIPC() const {
  return ownership_passes_to_ipc_;
}

}  // namespace base
