// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_BUFFER_ALLOCATOR_H_
#define NET_QUIC_CORE_QUIC_BUFFER_ALLOCATOR_H_

#include <stddef.h>

#include "net/quic/platform/api/quic_export.h"

namespace net {

// Abstract base class for classes which allocate and delete buffers.
class QUIC_EXPORT_PRIVATE QuicBufferAllocator {
 public:
  virtual ~QuicBufferAllocator();

  // Returns or allocates a new buffer of |size|. Never returns null.
  virtual char* New(size_t size) = 0;

  // Returns or allocates a new buffer of |size| if |flag_enable| is true.
  // Otherwise, returns a buffer that is compatible with this class directly
  // with operator new. Never returns null.
  virtual char* New(size_t size, bool flag_enable) = 0;

  // Releases a buffer.
  virtual void Delete(char* buffer) = 0;

  // Marks the allocator as being idle. Serves as a hint to notify the allocator
  // that it should release any resources it's still holding on to.
  virtual void MarkAllocatorIdle() {}
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_BUFFER_ALLOCATOR_H_
