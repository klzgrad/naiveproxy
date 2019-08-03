// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>
#include "base/allocator/partition_allocator/partition_alloc.h"
#include "base/time/time.h"
#include "base/timer/lap_timer.h"

#include "build/build_config.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "testing/perf/perf_test.h"

namespace base {
namespace {

// Change kTimeLimit to something higher if you need more time to capture a
// trace.
constexpr base::TimeDelta kTimeLimit = base::TimeDelta::FromSeconds(2);
constexpr int kWarmupRuns = 5;
constexpr int kTimeCheckInterval = 100000;

// Size constants are mostly arbitrary, but try to simulate something like CSS
// parsing which consists of lots of relatively small objects.
constexpr int kMultiBucketMinimumSize = 24;
constexpr int kMultiBucketIncrement = 13;
// Final size is 24 + (13 * 22) = 310 bytes.
constexpr int kMultiBucketRounds = 22;

class MemoryAllocationPerfTest : public testing::Test {
 public:
  MemoryAllocationPerfTest()
      : timer_(kWarmupRuns, kTimeLimit, kTimeCheckInterval) {}
  void SetUp() override { alloc_.init(); }
  void TearDown() override {
    alloc_.root()->PurgeMemory(PartitionPurgeDecommitEmptyPages |
                               PartitionPurgeDiscardUnusedSystemPages);
  }
  LapTimer timer_;
  PartitionAllocatorGeneric alloc_;
};

class MemoryAllocationPerfNode {
 public:
  MemoryAllocationPerfNode* GetNext() const { return next_; }
  void SetNext(MemoryAllocationPerfNode* p) { next_ = p; }
  static void FreeAll(MemoryAllocationPerfNode* first,
                      PartitionAllocatorGeneric& alloc) {
    MemoryAllocationPerfNode* cur = first;
    while (cur != nullptr) {
      MemoryAllocationPerfNode* next = cur->GetNext();
      alloc.root()->Free(cur);
      cur = next;
    }
  }

 private:
  MemoryAllocationPerfNode* next_ = nullptr;
};

TEST_F(MemoryAllocationPerfTest, SingleBucket) {
  timer_.Reset();
  MemoryAllocationPerfNode* first = reinterpret_cast<MemoryAllocationPerfNode*>(
      alloc_.root()->Alloc(40, "<testing>"));
  MemoryAllocationPerfNode* cur = first;
  do {
    MemoryAllocationPerfNode* next =
        reinterpret_cast<MemoryAllocationPerfNode*>(
            alloc_.root()->Alloc(40, "<testing>"));
    CHECK_NE(next, nullptr);
    cur->SetNext(next);
    cur = next;
    timer_.NextLap();
  } while (!timer_.HasTimeLimitExpired());
  // next_ = nullptr only works if the class constructor is called (it's not
  // called in this case because then we can allocate arbitrary-length
  // payloads.)
  cur->SetNext(nullptr);

  MemoryAllocationPerfNode::FreeAll(first, alloc_);

  perf_test::PrintResult("MemoryAllocationPerfTest",
                         " single bucket allocation (40 bytes)", "",
                         timer_.LapsPerSecond(), "runs/s", true);
}

TEST_F(MemoryAllocationPerfTest, SingleBucketWithFree) {
  timer_.Reset();
  // Allocate an initial element to make sure the bucket stays set up.
  void* elem = alloc_.root()->Alloc(40, "<testing>");
  do {
    void* cur = alloc_.root()->Alloc(40, "<testing>");
    CHECK_NE(cur, nullptr);
    alloc_.root()->Free(cur);
    timer_.NextLap();
  } while (!timer_.HasTimeLimitExpired());

  alloc_.root()->Free(elem);
  perf_test::PrintResult("MemoryAllocationPerfTest",
                         " single bucket allocation + free (40 bytes)", "",
                         timer_.LapsPerSecond(), "runs/s", true);
}

// Failing on Nexus5x: crbug.com/949838
#if defined(OS_ANDROID)
#define MAYBE_MultiBucket DISABLED_MultiBucket
#else
#define MAYBE_MultiBucket MultiBucket
#endif
TEST_F(MemoryAllocationPerfTest, MAYBE_MultiBucket) {
  timer_.Reset();
  MemoryAllocationPerfNode* first = reinterpret_cast<MemoryAllocationPerfNode*>(
      alloc_.root()->Alloc(40, "<testing>"));
  MemoryAllocationPerfNode* cur = first;
  do {
    for (int i = 0; i < kMultiBucketRounds; i++) {
      MemoryAllocationPerfNode* next =
          reinterpret_cast<MemoryAllocationPerfNode*>(alloc_.root()->Alloc(
              kMultiBucketMinimumSize + (i * kMultiBucketIncrement),
              "<testing>"));
      CHECK_NE(next, nullptr);
      cur->SetNext(next);
      cur = next;
    }
    timer_.NextLap();
  } while (!timer_.HasTimeLimitExpired());
  cur->SetNext(nullptr);

  MemoryAllocationPerfNode::FreeAll(first, alloc_);

  perf_test::PrintResult("MemoryAllocationPerfTest", " multi-bucket allocation",
                         "", timer_.LapsPerSecond() * kMultiBucketRounds,
                         "runs/s", true);
}

TEST_F(MemoryAllocationPerfTest, MultiBucketWithFree) {
  timer_.Reset();
  std::vector<void*> elems;
  // Do an initial round of allocation to make sure that the buckets stay in use
  // (and aren't accidentally released back to the OS).
  for (int i = 0; i < kMultiBucketRounds; i++) {
    void* cur = alloc_.root()->Alloc(
        kMultiBucketMinimumSize + (i * kMultiBucketIncrement), "<testing>");
    CHECK_NE(cur, nullptr);
    elems.push_back(cur);
  }

  do {
    for (int i = 0; i < kMultiBucketRounds; i++) {
      void* cur = alloc_.root()->Alloc(
          kMultiBucketMinimumSize + (i * kMultiBucketIncrement), "<testing>");
      CHECK_NE(cur, nullptr);
      alloc_.root()->Free(cur);
    }
    timer_.NextLap();
  } while (!timer_.HasTimeLimitExpired());

  for (void* ptr : elems) {
    alloc_.root()->Free(ptr);
  }

  perf_test::PrintResult(
      "MemoryAllocationPerfTest", " multi-bucket allocation + free", "",
      timer_.LapsPerSecond() * kMultiBucketRounds, "runs/s", true);
}

}  // anonymous namespace

}  // namespace base
