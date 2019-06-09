// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/aligned_memory.h"

#include <memory>

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#define EXPECT_ALIGNED(ptr, align) \
    EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(ptr) & (align - 1))

namespace base {

TEST(AlignedMemoryTest, DynamicAllocation) {
  void* p = AlignedAlloc(8, 8);
  EXPECT_TRUE(p);
  EXPECT_ALIGNED(p, 8);
  AlignedFree(p);

  p = AlignedAlloc(8, 16);
  EXPECT_TRUE(p);
  EXPECT_ALIGNED(p, 16);
  AlignedFree(p);

  p = AlignedAlloc(8, 256);
  EXPECT_TRUE(p);
  EXPECT_ALIGNED(p, 256);
  AlignedFree(p);

  p = AlignedAlloc(8, 4096);
  EXPECT_TRUE(p);
  EXPECT_ALIGNED(p, 4096);
  AlignedFree(p);
}

TEST(AlignedMemoryTest, ScopedDynamicAllocation) {
  std::unique_ptr<float, AlignedFreeDeleter> p(
      static_cast<float*>(AlignedAlloc(8, 8)));
  EXPECT_TRUE(p.get());
  EXPECT_ALIGNED(p.get(), 8);
}

}  // namespace base
