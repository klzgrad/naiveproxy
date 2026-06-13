// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_SIMPLE_BUFFER_ALLOCATOR_H_
#define QUICHE_COMMON_SIMPLE_BUFFER_ALLOCATOR_H_

#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace quiche {

// Provides buffer allocation using operators new[] and delete[] on char arrays.
// Note that some of the QUICHE code relies on this being the case for deleting
// new[]-allocated arrays from elsewhere.
class QUICHE_EXPORT SimpleBufferAllocator : public QuicheBufferAllocator {
 public:
  static SimpleBufferAllocator* Get() {
    static SimpleBufferAllocator* singleton = new SimpleBufferAllocator();
    return singleton;
  }

  char* New(size_t size) override;
  char* New(size_t size, bool flag_enable) override;
  void Delete(char* buffer) override;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_SIMPLE_BUFFER_ALLOCATOR_H_
