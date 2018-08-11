// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_handle.h"

namespace base {

SharedMemoryHandle::SharedMemoryHandle(const SharedMemoryHandle& handle) =
    default;

SharedMemoryHandle& SharedMemoryHandle::operator=(
    const SharedMemoryHandle& handle) = default;

base::UnguessableToken SharedMemoryHandle::GetGUID() const {
  return guid_;
}

size_t SharedMemoryHandle::GetSize() const {
  return size_;
}

}  // namespace base
