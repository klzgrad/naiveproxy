// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/simple_buffer_allocator.h"

namespace quiche {

char* SimpleBufferAllocator::New(size_t size) { return new char[size]; }

char* SimpleBufferAllocator::New(size_t size, bool /* flag_enable */) {
  return New(size);
}

void SimpleBufferAllocator::Delete(char* buffer) { delete[] buffer; }

}  // namespace quiche
