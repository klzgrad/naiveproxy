// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/platform_shared_memory_region.h"

#include "base/memory/shared_memory_mapping.h"

namespace base {
namespace subtle {

// static
PlatformSharedMemoryRegion PlatformSharedMemoryRegion::CreateWritable(
    size_t size) {
  return Create(Mode::kWritable, size);
}

// static
PlatformSharedMemoryRegion PlatformSharedMemoryRegion::CreateUnsafe(
    size_t size) {
  return Create(Mode::kUnsafe, size);
}

PlatformSharedMemoryRegion::PlatformSharedMemoryRegion() = default;
PlatformSharedMemoryRegion::PlatformSharedMemoryRegion(
    PlatformSharedMemoryRegion&& other) = default;
PlatformSharedMemoryRegion& PlatformSharedMemoryRegion::operator=(
    PlatformSharedMemoryRegion&& other) = default;
PlatformSharedMemoryRegion::~PlatformSharedMemoryRegion() = default;

PlatformSharedMemoryRegion::ScopedPlatformHandle
PlatformSharedMemoryRegion::PassPlatformHandle() {
  return std::move(handle_);
}

}  // namespace subtle
}  // namespace base
