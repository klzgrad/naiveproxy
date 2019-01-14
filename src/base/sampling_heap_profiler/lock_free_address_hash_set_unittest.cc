// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sampling_heap_profiler/lock_free_address_hash_set.h"

#include <stdlib.h>
#include <cinttypes>
#include <memory>

#include "base/allocator/allocator_shim.h"
#include "base/debug/alias.h"
#include "base/threading/simple_thread.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

class LockFreeAddressHashSetTest : public ::testing::Test {
 public:
  static bool Subset(const LockFreeAddressHashSet& superset,
                     const LockFreeAddressHashSet& subset) {
    for (subtle::AtomicWord bucket : subset.buckets_) {
      for (LockFreeAddressHashSet::Node* node =
               reinterpret_cast<LockFreeAddressHashSet::Node*>(bucket);
           node; node = LockFreeAddressHashSet::next_node(node)) {
        void* key = reinterpret_cast<void*>(node->key);
        if (key && !superset.Contains(key))
          return false;
      }
    }
    return true;
  }

  static bool Equals(const LockFreeAddressHashSet& set1,
                     const LockFreeAddressHashSet& set2) {
    return Subset(set1, set2) && Subset(set2, set1);
  }

  static size_t BucketSize(const LockFreeAddressHashSet& set, size_t bucket) {
    size_t count = 0;
    LockFreeAddressHashSet::Node* node =
        reinterpret_cast<LockFreeAddressHashSet::Node*>(set.buckets_[bucket]);
    for (; node; node = set.next_node(node))
      ++count;
    return count;
  }
};

namespace {

TEST_F(LockFreeAddressHashSetTest, EmptySet) {
  LockFreeAddressHashSet set(8);
  EXPECT_EQ(size_t(0), set.size());
  EXPECT_EQ(size_t(8), set.buckets_count());
  EXPECT_EQ(0., set.load_factor());
  EXPECT_FALSE(set.Contains(&set));
}

TEST_F(LockFreeAddressHashSetTest, BasicOperations) {
  LockFreeAddressHashSet set(8);

  for (size_t i = 1; i <= 100; ++i) {
    void* ptr = reinterpret_cast<void*>(i);
    set.Insert(ptr);
    EXPECT_EQ(i, set.size());
    EXPECT_TRUE(set.Contains(ptr));
  }

  size_t size = 100;
  EXPECT_EQ(size, set.size());
  EXPECT_EQ(size_t(8), set.buckets_count());
  EXPECT_EQ(size / 8., set.load_factor());

  for (size_t i = 99; i >= 3; i -= 3) {
    void* ptr = reinterpret_cast<void*>(i);
    set.Remove(ptr);
    EXPECT_EQ(--size, set.size());
    EXPECT_FALSE(set.Contains(ptr));
  }
  // Removed every 3rd value (33 total) from the set, 67 have left.
  EXPECT_EQ(size_t(67), set.size());

  for (size_t i = 1; i <= 100; ++i) {
    void* ptr = reinterpret_cast<void*>(i);
    EXPECT_EQ(i % 3 != 0, set.Contains(ptr));
  }
}

TEST_F(LockFreeAddressHashSetTest, Copy) {
  LockFreeAddressHashSet set(16);

  for (size_t i = 1000; i <= 16000; i += 1000) {
    void* ptr = reinterpret_cast<void*>(i);
    set.Insert(ptr);
  }

  LockFreeAddressHashSet set2(4);
  LockFreeAddressHashSet set3(64);
  set2.Copy(set);
  set3.Copy(set);

  EXPECT_TRUE(Equals(set, set2));
  EXPECT_TRUE(Equals(set, set3));
  EXPECT_TRUE(Equals(set2, set3));

  set.Insert(reinterpret_cast<void*>(42));

  EXPECT_FALSE(Equals(set, set2));
  EXPECT_FALSE(Equals(set, set3));
  EXPECT_TRUE(Equals(set2, set3));

  EXPECT_TRUE(Subset(set, set2));
  EXPECT_FALSE(Subset(set2, set));
}

class WriterThread : public SimpleThread {
 public:
  WriterThread(LockFreeAddressHashSet* set, subtle::Atomic32* cancel)
      : SimpleThread("ReaderThread"), set_(set), cancel_(cancel) {}

  void Run() override {
    for (size_t value = 42; !subtle::Acquire_Load(cancel_); ++value) {
      void* ptr = reinterpret_cast<void*>(value);
      set_->Insert(ptr);
      EXPECT_TRUE(set_->Contains(ptr));
      set_->Remove(ptr);
      EXPECT_FALSE(set_->Contains(ptr));
    }
    // Leave a key for reader to test.
    set_->Insert(reinterpret_cast<void*>(0x1337));
  }

 private:
  LockFreeAddressHashSet* set_;
  subtle::Atomic32* cancel_;
};

#if defined(THREAD_SANITIZER)
#define DISABLE_ON_TSAN(test_name) DISABLED_##test_name
#else
#define DISABLE_ON_TSAN(test_name) test_name
#endif  // defined(THREAD_SANITIZER)
TEST_F(LockFreeAddressHashSetTest, DISABLE_ON_TSAN(ConcurrentAccess)) {
  // The purpose of this test is to make sure adding/removing keys concurrently
  // does not disrupt the state of other keys.
  LockFreeAddressHashSet set(16);
  subtle::Atomic32 cancel = 0;
  auto thread = std::make_unique<WriterThread>(&set, &cancel);
  thread->Start();
  for (size_t i = 1; i <= 20; ++i)
    set.Insert(reinterpret_cast<void*>(i));
  // Remove some items to test empty nodes.
  for (size_t i = 16; i <= 20; ++i)
    set.Remove(reinterpret_cast<void*>(i));
  for (size_t k = 0; k < 100000; ++k) {
    for (size_t i = 1; i <= 30; ++i) {
      EXPECT_EQ(i < 16, set.Contains(reinterpret_cast<void*>(i)));
    }
  }
  subtle::Release_Store(&cancel, 1);
  thread->Join();

  EXPECT_TRUE(set.Contains(reinterpret_cast<void*>(0x1337)));
  EXPECT_FALSE(set.Contains(reinterpret_cast<void*>(0xbadf00d)));
}

TEST_F(LockFreeAddressHashSetTest, BucketsUsage) {
  // Test the uniformity of buckets usage.
  size_t count = 10000;
  LockFreeAddressHashSet set(16);
  for (size_t i = 0; i < count; ++i)
    set.Insert(reinterpret_cast<void*>(0x10000 + 0x10 * i));
  size_t average_per_bucket = count / set.buckets_count();
  for (size_t i = 0; i < set.buckets_count(); ++i) {
    size_t usage = BucketSize(set, i);
    EXPECT_LT(average_per_bucket * 95 / 100, usage);
    EXPECT_GT(average_per_bucket * 105 / 100, usage);
  }
}

}  // namespace
}  // namespace base
