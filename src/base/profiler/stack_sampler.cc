// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/stack_sampler.h"

#include "base/memory/ptr_util.h"

namespace base {

constexpr size_t StackSampler::StackBuffer::kPlatformStackAlignment;

StackSampler::StackBuffer::StackBuffer(size_t buffer_size)
    : buffer_(new uint8_t[buffer_size + kPlatformStackAlignment - 1]),
      size_(buffer_size) {}

StackSampler::StackBuffer::~StackBuffer() = default;

StackSampler::StackSampler() = default;

StackSampler::~StackSampler() = default;

std::unique_ptr<StackSampler::StackBuffer> StackSampler::CreateStackBuffer() {
  size_t size = GetStackBufferSize();
  if (size == 0)
    return nullptr;
  return std::make_unique<StackBuffer>(size);
}

StackSamplerTestDelegate::~StackSamplerTestDelegate() = default;

StackSamplerTestDelegate::StackSamplerTestDelegate() = default;

}  // namespace base
