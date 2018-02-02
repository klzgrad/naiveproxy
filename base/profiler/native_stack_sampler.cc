// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/native_stack_sampler.h"

#include "base/memory/ptr_util.h"

namespace base {

NativeStackSampler::StackBuffer::StackBuffer(size_t buffer_size)
    : buffer_(new uintptr_t[(buffer_size + sizeof(uintptr_t) - 1) /
                            sizeof(uintptr_t)]),
      size_(buffer_size) {}

NativeStackSampler::StackBuffer::~StackBuffer() = default;

NativeStackSampler::NativeStackSampler() = default;

NativeStackSampler::~NativeStackSampler() = default;

std::unique_ptr<NativeStackSampler::StackBuffer>
NativeStackSampler::CreateStackBuffer() {
  size_t size = GetStackBufferSize();
  if (size == 0)
    return nullptr;
  return std::make_unique<StackBuffer>(size);
}

NativeStackSamplerTestDelegate::~NativeStackSamplerTestDelegate() = default;

NativeStackSamplerTestDelegate::NativeStackSamplerTestDelegate() = default;

}  // namespace base
