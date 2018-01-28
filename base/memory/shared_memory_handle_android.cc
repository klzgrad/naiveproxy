// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_handle.h"

#include <unistd.h>

#include "base/android/android_hardware_buffer_compat.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/posix/unix_domain_socket.h"
#include "base/unguessable_token.h"

namespace base {

SharedMemoryHandle::SharedMemoryHandle() {}

SharedMemoryHandle::SharedMemoryHandle(
    const base::FileDescriptor& file_descriptor,
    size_t size,
    const base::UnguessableToken& guid)
    : SharedMemoryHandle(Type::ASHMEM, file_descriptor, size, guid) {}

SharedMemoryHandle::SharedMemoryHandle(
    Type type,
    const base::FileDescriptor& file_descriptor,
    size_t size,
    const base::UnguessableToken& guid)
    : type_(type), guid_(guid), size_(size) {
  switch (type) {
    case Type::INVALID:
      NOTREACHED() << "Can't create a Type::INVALID from a file descriptor";
      break;
    case Type::ASHMEM:
      DCHECK_GE(file_descriptor.fd, 0);
      file_descriptor_ = file_descriptor;
      break;
    case Type::ANDROID_HARDWARE_BUFFER:
      // This may be the first use of AHardwareBuffers in this process, so we
      // need to load symbols. This should not fail since we're supposedly
      // receiving one from IPC, but better to be paranoid.
      if (!AndroidHardwareBufferCompat::IsSupportAvailable()) {
        NOTREACHED() << "AHardwareBuffer support not available";
        type_ = Type::INVALID;
        return;
      }

      AHardwareBuffer* ahb = nullptr;
      // A successful receive increments refcount, we don't need to do so
      // separately.
      int ret =
          AndroidHardwareBufferCompat::GetInstance().RecvHandleFromUnixSocket(
              file_descriptor.fd, &ahb);

      // We need to take ownership of the FD and close it if it came from IPC.
      if (file_descriptor.auto_close) {
        if (IGNORE_EINTR(close(file_descriptor.fd)) < 0)
          PLOG(ERROR) << "close";
      }

      if (ret < 0) {
        PLOG(ERROR) << "recv";
        type_ = Type::INVALID;
        return;
      }

      memory_object_ = ahb;
  }
}

SharedMemoryHandle::SharedMemoryHandle(AHardwareBuffer* buffer,
                                       size_t size,
                                       const base::UnguessableToken& guid)
    : type_(Type::ANDROID_HARDWARE_BUFFER),
      memory_object_(buffer),
      ownership_passes_to_ipc_(false),
      guid_(guid),
      size_(size) {
  // Don't call Acquire on the AHardwareBuffer here. Getting a handle doesn't
  // take ownership.
}

// static
SharedMemoryHandle SharedMemoryHandle::ImportHandle(int fd, size_t size) {
  SharedMemoryHandle handle;
  handle.type_ = Type::ASHMEM;
  handle.file_descriptor_.fd = fd;
  handle.file_descriptor_.auto_close = false;
  handle.guid_ = UnguessableToken::Create();
  handle.size_ = size;
  return handle;
}

int SharedMemoryHandle::GetHandle() const {
  switch (type_) {
    case Type::INVALID:
      return -1;
    case Type::ASHMEM:
      DCHECK(IsValid());
      return file_descriptor_.fd;
    case Type::ANDROID_HARDWARE_BUFFER:
      DCHECK(IsValid());
      ScopedFD read_fd, write_fd;
      if (!CreateSocketPair(&read_fd, &write_fd)) {
        PLOG(ERROR) << "SocketPair";
        return -1;
      }

      int ret =
          AndroidHardwareBufferCompat::GetInstance().SendHandleToUnixSocket(
              memory_object_, write_fd.get());
      if (ret < 0) {
        PLOG(ERROR) << "send";
        return -1;
      }

      // Close write end now to avoid timeouts in case the receiver goes away.
      write_fd.reset();

      return read_fd.release();
  }
}

bool SharedMemoryHandle::IsValid() const {
  return type_ != Type::INVALID;
}

void SharedMemoryHandle::Close() const {
  switch (type_) {
    case Type::INVALID:
      return;
    case Type::ASHMEM:
      DCHECK(IsValid());
      if (IGNORE_EINTR(close(file_descriptor_.fd)) < 0)
        PLOG(ERROR) << "close";
      break;
    case Type::ANDROID_HARDWARE_BUFFER:
      DCHECK(IsValid());
      AndroidHardwareBufferCompat::GetInstance().Release(memory_object_);
  }
}

int SharedMemoryHandle::Release() {
  DCHECK_EQ(type_, Type::ASHMEM);
  int old_fd = file_descriptor_.fd;
  file_descriptor_.fd = -1;
  return old_fd;
}

SharedMemoryHandle SharedMemoryHandle::Duplicate() const {
  switch (type_) {
    case Type::INVALID:
      return SharedMemoryHandle();
    case Type::ASHMEM: {
      DCHECK(IsValid());
      int duped_handle = HANDLE_EINTR(dup(file_descriptor_.fd));
      if (duped_handle < 0)
        return SharedMemoryHandle();
      return SharedMemoryHandle(FileDescriptor(duped_handle, true), GetSize(),
                                GetGUID());
    }
    case Type::ANDROID_HARDWARE_BUFFER:
      DCHECK(IsValid());
      AndroidHardwareBufferCompat::GetInstance().Acquire(memory_object_);
      SharedMemoryHandle handle(*this);
      handle.SetOwnershipPassesToIPC(true);
      return handle;
  }
}

AHardwareBuffer* SharedMemoryHandle::GetMemoryObject() const {
  DCHECK_EQ(type_, Type::ANDROID_HARDWARE_BUFFER);
  return memory_object_;
}

void SharedMemoryHandle::SetOwnershipPassesToIPC(bool ownership_passes) {
  switch (type_) {
    case Type::ASHMEM:
      file_descriptor_.auto_close = ownership_passes;
      break;
    case Type::INVALID:
    case Type::ANDROID_HARDWARE_BUFFER:
      ownership_passes_to_ipc_ = ownership_passes;
  }
}

bool SharedMemoryHandle::OwnershipPassesToIPC() const {
  switch (type_) {
    case Type::ASHMEM:
      return file_descriptor_.auto_close;
    case Type::INVALID:
    case Type::ANDROID_HARDWARE_BUFFER:
      return ownership_passes_to_ipc_;
  }
}

}  // namespace base
