// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/thread_heap_usage_tracker.h"

#include <map>

#include "base/allocator/allocator_shim.h"
#include "base/allocator/buildflags.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_MACOSX)
#include "base/allocator/allocator_interception_mac.h"
#endif

namespace base {
namespace debug {

namespace {

class TestingThreadHeapUsageTracker : public ThreadHeapUsageTracker {
 public:
  using ThreadHeapUsageTracker::DisableHeapTrackingForTesting;
  using ThreadHeapUsageTracker::EnsureTLSInitialized;
  using ThreadHeapUsageTracker::GetDispatchForTesting;
};

// A fixture class that allows testing the AllocatorDispatch associated with
// the ThreadHeapUsageTracker class in isolation against a mocked
// underlying
// heap implementation.
class ThreadHeapUsageTrackerTest : public testing::Test {
 public:
  using AllocatorDispatch = base::allocator::AllocatorDispatch;

  static const size_t kAllocationPadding;
  enum SizeFunctionKind {
    EXACT_SIZE_FUNCTION,
    PADDING_SIZE_FUNCTION,
    ZERO_SIZE_FUNCTION,
  };

  ThreadHeapUsageTrackerTest() : size_function_kind_(EXACT_SIZE_FUNCTION) {
    EXPECT_EQ(nullptr, g_self);
    g_self = this;
  }

  ~ThreadHeapUsageTrackerTest() override {
    EXPECT_EQ(this, g_self);
    g_self = nullptr;
  }

  void set_size_function_kind(SizeFunctionKind kind) {
    size_function_kind_ = kind;
  }

  void SetUp() override {
    TestingThreadHeapUsageTracker::EnsureTLSInitialized();

    dispatch_under_test_ =
        TestingThreadHeapUsageTracker::GetDispatchForTesting();
    ASSERT_EQ(nullptr, dispatch_under_test_->next);

    dispatch_under_test_->next = &g_mock_dispatch;
  }

  void TearDown() override {
    ASSERT_EQ(&g_mock_dispatch, dispatch_under_test_->next);

    dispatch_under_test_->next = nullptr;
  }

  void* MockMalloc(size_t size) {
    return dispatch_under_test_->alloc_function(dispatch_under_test_, size,
                                                nullptr);
  }

  void* MockCalloc(size_t n, size_t size) {
    return dispatch_under_test_->alloc_zero_initialized_function(
        dispatch_under_test_, n, size, nullptr);
  }

  void* MockAllocAligned(size_t alignment, size_t size) {
    return dispatch_under_test_->alloc_aligned_function(
        dispatch_under_test_, alignment, size, nullptr);
  }

  void* MockRealloc(void* address, size_t size) {
    return dispatch_under_test_->realloc_function(dispatch_under_test_, address,
                                                  size, nullptr);
  }

  void MockFree(void* address) {
    dispatch_under_test_->free_function(dispatch_under_test_, address, nullptr);
  }

  size_t MockGetSizeEstimate(void* address) {
    return dispatch_under_test_->get_size_estimate_function(
        dispatch_under_test_, address, nullptr);
  }

 private:
  void RecordAlloc(void* address, size_t size) {
    if (address != nullptr)
      allocation_size_map_[address] = size;
  }

  void DeleteAlloc(void* address) {
    if (address != nullptr)
      EXPECT_EQ(1U, allocation_size_map_.erase(address));
  }

  size_t GetSizeEstimate(void* address) {
    auto it = allocation_size_map_.find(address);
    if (it == allocation_size_map_.end())
      return 0;

    size_t ret = it->second;
    switch (size_function_kind_) {
      case EXACT_SIZE_FUNCTION:
        break;
      case PADDING_SIZE_FUNCTION:
        ret += kAllocationPadding;
        break;
      case ZERO_SIZE_FUNCTION:
        ret = 0;
        break;
    }

    return ret;
  }

  static void* OnAllocFn(const AllocatorDispatch* self,
                         size_t size,
                         void* context) {
    EXPECT_EQ(&g_mock_dispatch, self);

    void* ret = malloc(size);
    g_self->RecordAlloc(ret, size);
    return ret;
  }

  static void* OnAllocZeroInitializedFn(const AllocatorDispatch* self,
                                        size_t n,
                                        size_t size,
                                        void* context) {
    EXPECT_EQ(&g_mock_dispatch, self);

    void* ret = calloc(n, size);
    g_self->RecordAlloc(ret, n * size);
    return ret;
  }

  static void* OnAllocAlignedFn(const AllocatorDispatch* self,
                                size_t alignment,
                                size_t size,
                                void* context) {
    EXPECT_EQ(&g_mock_dispatch, self);

    // This is a cheat as it doesn't return aligned allocations. This has the
    // advantage of working for all platforms for this test.
    void* ret = malloc(size);
    g_self->RecordAlloc(ret, size);
    return ret;
  }

  static void* OnReallocFn(const AllocatorDispatch* self,
                           void* address,
                           size_t size,
                           void* context) {
    EXPECT_EQ(&g_mock_dispatch, self);

    g_self->DeleteAlloc(address);
    void* ret = realloc(address, size);
    g_self->RecordAlloc(ret, size);
    return ret;
  }

  static void OnFreeFn(const AllocatorDispatch* self,
                       void* address,
                       void* context) {
    EXPECT_EQ(&g_mock_dispatch, self);

    g_self->DeleteAlloc(address);
    free(address);
  }

  static size_t OnGetSizeEstimateFn(const AllocatorDispatch* self,
                                    void* address,
                                    void* context) {
    EXPECT_EQ(&g_mock_dispatch, self);

    return g_self->GetSizeEstimate(address);
  }

  using AllocationSizeMap = std::map<void*, size_t>;

  SizeFunctionKind size_function_kind_;
  AllocationSizeMap allocation_size_map_;
  AllocatorDispatch* dispatch_under_test_;

  static base::allocator::AllocatorDispatch g_mock_dispatch;
  static ThreadHeapUsageTrackerTest* g_self;
};

const size_t ThreadHeapUsageTrackerTest::kAllocationPadding = 23;

ThreadHeapUsageTrackerTest* ThreadHeapUsageTrackerTest::g_self = nullptr;

base::allocator::AllocatorDispatch ThreadHeapUsageTrackerTest::g_mock_dispatch =
    {
        &ThreadHeapUsageTrackerTest::OnAllocFn,  // alloc_function
        &ThreadHeapUsageTrackerTest::
            OnAllocZeroInitializedFn,  // alloc_zero_initialized_function
        &ThreadHeapUsageTrackerTest::
            OnAllocAlignedFn,                      // alloc_aligned_function
        &ThreadHeapUsageTrackerTest::OnReallocFn,  // realloc_function
        &ThreadHeapUsageTrackerTest::OnFreeFn,     // free_function
        &ThreadHeapUsageTrackerTest::
            OnGetSizeEstimateFn,  // get_size_estimate_function
        nullptr,                  // batch_malloc
        nullptr,                  // batch_free
        nullptr,                  // free_definite_size_function
        nullptr,                  // next
};

}  // namespace

TEST_F(ThreadHeapUsageTrackerTest, SimpleUsageWithExactSizeFunction) {
  set_size_function_kind(EXACT_SIZE_FUNCTION);

  ThreadHeapUsageTracker usage_tracker;
  usage_tracker.Start();

  ThreadHeapUsage u1 = ThreadHeapUsageTracker::GetUsageSnapshot();

  EXPECT_EQ(0U, u1.alloc_ops);
  EXPECT_EQ(0U, u1.alloc_bytes);
  EXPECT_EQ(0U, u1.alloc_overhead_bytes);
  EXPECT_EQ(0U, u1.free_ops);
  EXPECT_EQ(0U, u1.free_bytes);
  EXPECT_EQ(0U, u1.max_allocated_bytes);

  const size_t kAllocSize = 1029U;
  void* ptr = MockMalloc(kAllocSize);
  MockFree(ptr);

  usage_tracker.Stop(false);
  ThreadHeapUsage u2 = usage_tracker.usage();

  EXPECT_EQ(1U, u2.alloc_ops);
  EXPECT_EQ(kAllocSize, u2.alloc_bytes);
  EXPECT_EQ(0U, u2.alloc_overhead_bytes);
  EXPECT_EQ(1U, u2.free_ops);
  EXPECT_EQ(kAllocSize, u2.free_bytes);
  EXPECT_EQ(kAllocSize, u2.max_allocated_bytes);
}

TEST_F(ThreadHeapUsageTrackerTest, SimpleUsageWithPaddingSizeFunction) {
  set_size_function_kind(PADDING_SIZE_FUNCTION);

  ThreadHeapUsageTracker usage_tracker;
  usage_tracker.Start();

  ThreadHeapUsage u1 = ThreadHeapUsageTracker::GetUsageSnapshot();

  EXPECT_EQ(0U, u1.alloc_ops);
  EXPECT_EQ(0U, u1.alloc_bytes);
  EXPECT_EQ(0U, u1.alloc_overhead_bytes);
  EXPECT_EQ(0U, u1.free_ops);
  EXPECT_EQ(0U, u1.free_bytes);
  EXPECT_EQ(0U, u1.max_allocated_bytes);

  const size_t kAllocSize = 1029U;
  void* ptr = MockMalloc(kAllocSize);
  MockFree(ptr);

  usage_tracker.Stop(false);
  ThreadHeapUsage u2 = usage_tracker.usage();

  EXPECT_EQ(1U, u2.alloc_ops);
  EXPECT_EQ(kAllocSize + kAllocationPadding, u2.alloc_bytes);
  EXPECT_EQ(kAllocationPadding, u2.alloc_overhead_bytes);
  EXPECT_EQ(1U, u2.free_ops);
  EXPECT_EQ(kAllocSize + kAllocationPadding, u2.free_bytes);
  EXPECT_EQ(kAllocSize + kAllocationPadding, u2.max_allocated_bytes);
}

TEST_F(ThreadHeapUsageTrackerTest, SimpleUsageWithZeroSizeFunction) {
  set_size_function_kind(ZERO_SIZE_FUNCTION);

  ThreadHeapUsageTracker usage_tracker;
  usage_tracker.Start();

  ThreadHeapUsage u1 = ThreadHeapUsageTracker::GetUsageSnapshot();
  EXPECT_EQ(0U, u1.alloc_ops);
  EXPECT_EQ(0U, u1.alloc_bytes);
  EXPECT_EQ(0U, u1.alloc_overhead_bytes);
  EXPECT_EQ(0U, u1.free_ops);
  EXPECT_EQ(0U, u1.free_bytes);
  EXPECT_EQ(0U, u1.max_allocated_bytes);

  const size_t kAllocSize = 1029U;
  void* ptr = MockMalloc(kAllocSize);
  MockFree(ptr);

  usage_tracker.Stop(false);
  ThreadHeapUsage u2 = usage_tracker.usage();

  // With a get-size function that returns zero, there's no way to get the size
  // of an allocation that's being freed, hence the shim can't tally freed bytes
  // nor the high-watermark allocated bytes.
  EXPECT_EQ(1U, u2.alloc_ops);
  EXPECT_EQ(kAllocSize, u2.alloc_bytes);
  EXPECT_EQ(0U, u2.alloc_overhead_bytes);
  EXPECT_EQ(1U, u2.free_ops);
  EXPECT_EQ(0U, u2.free_bytes);
  EXPECT_EQ(0U, u2.max_allocated_bytes);
}

TEST_F(ThreadHeapUsageTrackerTest, ReallocCorrectlyTallied) {
  const size_t kAllocSize = 237U;

  {
    ThreadHeapUsageTracker usage_tracker;
    usage_tracker.Start();

    // Reallocating nullptr should count as a single alloc.
    void* ptr = MockRealloc(nullptr, kAllocSize);
    ThreadHeapUsage usage = ThreadHeapUsageTracker::GetUsageSnapshot();
    EXPECT_EQ(1U, usage.alloc_ops);
    EXPECT_EQ(kAllocSize, usage.alloc_bytes);
    EXPECT_EQ(0U, usage.alloc_overhead_bytes);
    EXPECT_EQ(0U, usage.free_ops);
    EXPECT_EQ(0U, usage.free_bytes);
    EXPECT_EQ(kAllocSize, usage.max_allocated_bytes);

    // Reallocating a valid pointer to a zero size should count as a single
    // free.
    ptr = MockRealloc(ptr, 0U);

    usage_tracker.Stop(false);
    EXPECT_EQ(1U, usage_tracker.usage().alloc_ops);
    EXPECT_EQ(kAllocSize, usage_tracker.usage().alloc_bytes);
    EXPECT_EQ(0U, usage_tracker.usage().alloc_overhead_bytes);
    EXPECT_EQ(1U, usage_tracker.usage().free_ops);
    EXPECT_EQ(kAllocSize, usage_tracker.usage().free_bytes);
    EXPECT_EQ(kAllocSize, usage_tracker.usage().max_allocated_bytes);

    // Realloc to zero size may or may not return a nullptr - make sure to
    // free the zero-size alloc in the latter case.
    if (ptr != nullptr)
      MockFree(ptr);
  }

  {
    ThreadHeapUsageTracker usage_tracker;
    usage_tracker.Start();

    void* ptr = MockMalloc(kAllocSize);
    ThreadHeapUsage usage = ThreadHeapUsageTracker::GetUsageSnapshot();
    EXPECT_EQ(1U, usage.alloc_ops);

    // Now try reallocating a valid pointer to a larger size, this should count
    // as one free and one alloc.
    const size_t kLargerAllocSize = kAllocSize + 928U;
    ptr = MockRealloc(ptr, kLargerAllocSize);

    usage_tracker.Stop(false);
    EXPECT_EQ(2U, usage_tracker.usage().alloc_ops);
    EXPECT_EQ(kAllocSize + kLargerAllocSize, usage_tracker.usage().alloc_bytes);
    EXPECT_EQ(0U, usage_tracker.usage().alloc_overhead_bytes);
    EXPECT_EQ(1U, usage_tracker.usage().free_ops);
    EXPECT_EQ(kAllocSize, usage_tracker.usage().free_bytes);
    EXPECT_EQ(kLargerAllocSize, usage_tracker.usage().max_allocated_bytes);

    MockFree(ptr);
  }
}

TEST_F(ThreadHeapUsageTrackerTest, NestedMaxWorks) {
  ThreadHeapUsageTracker usage_tracker;
  usage_tracker.Start();

  const size_t kOuterAllocSize = 1029U;
  void* ptr = MockMalloc(kOuterAllocSize);
  MockFree(ptr);

  EXPECT_EQ(kOuterAllocSize,
            ThreadHeapUsageTracker::GetUsageSnapshot().max_allocated_bytes);

  {
    ThreadHeapUsageTracker inner_usage_tracker;
    inner_usage_tracker.Start();

    const size_t kInnerAllocSize = 673U;
    ptr = MockMalloc(kInnerAllocSize);
    MockFree(ptr);

    inner_usage_tracker.Stop(false);

    EXPECT_EQ(kInnerAllocSize, inner_usage_tracker.usage().max_allocated_bytes);
  }

  // The greater, outer allocation size should have been restored.
  EXPECT_EQ(kOuterAllocSize,
            ThreadHeapUsageTracker::GetUsageSnapshot().max_allocated_bytes);

  const size_t kLargerInnerAllocSize = kOuterAllocSize + 673U;
  {
    ThreadHeapUsageTracker inner_usage_tracker;
    inner_usage_tracker.Start();

    ptr = MockMalloc(kLargerInnerAllocSize);
    MockFree(ptr);

    inner_usage_tracker.Stop(false);
    EXPECT_EQ(kLargerInnerAllocSize,
              inner_usage_tracker.usage().max_allocated_bytes);
  }

  // The greater, inner allocation size should have been preserved.
  EXPECT_EQ(kLargerInnerAllocSize,
            ThreadHeapUsageTracker::GetUsageSnapshot().max_allocated_bytes);

  // Now try the case with an outstanding net alloc size when entering the
  // inner scope.
  void* outer_ptr = MockMalloc(kOuterAllocSize);
  EXPECT_EQ(kLargerInnerAllocSize,
            ThreadHeapUsageTracker::GetUsageSnapshot().max_allocated_bytes);
  {
    ThreadHeapUsageTracker inner_usage_tracker;
    inner_usage_tracker.Start();

    ptr = MockMalloc(kLargerInnerAllocSize);
    MockFree(ptr);

    inner_usage_tracker.Stop(false);
    EXPECT_EQ(kLargerInnerAllocSize,
              inner_usage_tracker.usage().max_allocated_bytes);
  }

  // While the inner scope saw only the inner net outstanding allocation size,
  // the outer scope saw both outstanding at the same time.
  EXPECT_EQ(kOuterAllocSize + kLargerInnerAllocSize,
            ThreadHeapUsageTracker::GetUsageSnapshot().max_allocated_bytes);

  MockFree(outer_ptr);

  // Test a net-negative scope.
  ptr = MockMalloc(kLargerInnerAllocSize);
  {
    ThreadHeapUsageTracker inner_usage_tracker;
    inner_usage_tracker.Start();

    MockFree(ptr);

    const size_t kInnerAllocSize = 1;
    ptr = MockMalloc(kInnerAllocSize);

    inner_usage_tracker.Stop(false);
    // Since the scope is still net-negative, the max is clamped at zero.
    EXPECT_EQ(0U, inner_usage_tracker.usage().max_allocated_bytes);
  }

  MockFree(ptr);
}

TEST_F(ThreadHeapUsageTrackerTest, NoStopImpliesInclusive) {
  ThreadHeapUsageTracker usage_tracker;
  usage_tracker.Start();

  const size_t kOuterAllocSize = 1029U;
  void* ptr = MockMalloc(kOuterAllocSize);
  MockFree(ptr);

  ThreadHeapUsage usage = ThreadHeapUsageTracker::GetUsageSnapshot();
  EXPECT_EQ(kOuterAllocSize, usage.max_allocated_bytes);

  const size_t kInnerLargerAllocSize = kOuterAllocSize + 673U;

  {
    ThreadHeapUsageTracker inner_usage_tracker;
    inner_usage_tracker.Start();

    // Make a larger allocation than the outer scope.
    ptr = MockMalloc(kInnerLargerAllocSize);
    MockFree(ptr);

    // inner_usage_tracker goes out of scope without a Stop().
  }

  ThreadHeapUsage current = ThreadHeapUsageTracker::GetUsageSnapshot();
  EXPECT_EQ(usage.alloc_ops + 1, current.alloc_ops);
  EXPECT_EQ(usage.alloc_bytes + kInnerLargerAllocSize, current.alloc_bytes);
  EXPECT_EQ(usage.free_ops + 1, current.free_ops);
  EXPECT_EQ(usage.free_bytes + kInnerLargerAllocSize, current.free_bytes);
  EXPECT_EQ(kInnerLargerAllocSize, current.max_allocated_bytes);
}

TEST_F(ThreadHeapUsageTrackerTest, ExclusiveScopesWork) {
  ThreadHeapUsageTracker usage_tracker;
  usage_tracker.Start();

  const size_t kOuterAllocSize = 1029U;
  void* ptr = MockMalloc(kOuterAllocSize);
  MockFree(ptr);

  ThreadHeapUsage usage = ThreadHeapUsageTracker::GetUsageSnapshot();
  EXPECT_EQ(kOuterAllocSize, usage.max_allocated_bytes);

  {
    ThreadHeapUsageTracker inner_usage_tracker;
    inner_usage_tracker.Start();

    // Make a larger allocation than the outer scope.
    ptr = MockMalloc(kOuterAllocSize + 673U);
    MockFree(ptr);

    // This tracker is exlusive, all activity should be private to this scope.
    inner_usage_tracker.Stop(true);
  }

  ThreadHeapUsage current = ThreadHeapUsageTracker::GetUsageSnapshot();
  EXPECT_EQ(usage.alloc_ops, current.alloc_ops);
  EXPECT_EQ(usage.alloc_bytes, current.alloc_bytes);
  EXPECT_EQ(usage.alloc_overhead_bytes, current.alloc_overhead_bytes);
  EXPECT_EQ(usage.free_ops, current.free_ops);
  EXPECT_EQ(usage.free_bytes, current.free_bytes);
  EXPECT_EQ(usage.max_allocated_bytes, current.max_allocated_bytes);
}

TEST_F(ThreadHeapUsageTrackerTest, AllShimFunctionsAreProvided) {
  const size_t kAllocSize = 100;
  void* alloc = MockMalloc(kAllocSize);
  size_t estimate = MockGetSizeEstimate(alloc);
  ASSERT_TRUE(estimate == 0 || estimate >= kAllocSize);
  MockFree(alloc);

  alloc = MockCalloc(kAllocSize, 1);
  estimate = MockGetSizeEstimate(alloc);
  ASSERT_TRUE(estimate == 0 || estimate >= kAllocSize);
  MockFree(alloc);

  alloc = MockAllocAligned(1, kAllocSize);
  estimate = MockGetSizeEstimate(alloc);
  ASSERT_TRUE(estimate == 0 || estimate >= kAllocSize);

  alloc = MockRealloc(alloc, kAllocSize);
  estimate = MockGetSizeEstimate(alloc);
  ASSERT_TRUE(estimate == 0 || estimate >= kAllocSize);
  MockFree(alloc);
}

#if BUILDFLAG(USE_ALLOCATOR_SHIM)
class ThreadHeapUsageShimTest : public testing::Test {
#if defined(OS_MACOSX)
  void SetUp() override { allocator::InitializeAllocatorShim(); }
  void TearDown() override { allocator::UninterceptMallocZonesForTesting(); }
#endif
};

TEST_F(ThreadHeapUsageShimTest, HooksIntoMallocWhenShimAvailable) {
  ASSERT_FALSE(ThreadHeapUsageTracker::IsHeapTrackingEnabled());

  ThreadHeapUsageTracker::EnableHeapTracking();

  ASSERT_TRUE(ThreadHeapUsageTracker::IsHeapTrackingEnabled());

  const size_t kAllocSize = 9993;
  // This test verifies that the scoped heap data is affected by malloc &
  // free only when the shim is available.
  ThreadHeapUsageTracker usage_tracker;
  usage_tracker.Start();

  ThreadHeapUsage u1 = ThreadHeapUsageTracker::GetUsageSnapshot();
  void* ptr = malloc(kAllocSize);
  // Prevent the compiler from optimizing out the malloc/free pair.
  ASSERT_NE(nullptr, ptr);

  ThreadHeapUsage u2 = ThreadHeapUsageTracker::GetUsageSnapshot();
  free(ptr);

  usage_tracker.Stop(false);
  ThreadHeapUsage u3 = usage_tracker.usage();

  // Verify that at least one allocation operation was recorded, and that free
  // operations are at least monotonically growing.
  EXPECT_LE(0U, u1.alloc_ops);
  EXPECT_LE(u1.alloc_ops + 1, u2.alloc_ops);
  EXPECT_LE(u1.alloc_ops + 1, u3.alloc_ops);

  // Verify that at least the bytes above were recorded.
  EXPECT_LE(u1.alloc_bytes + kAllocSize, u2.alloc_bytes);

  // Verify that at least the one free operation above was recorded.
  EXPECT_LE(u2.free_ops + 1, u3.free_ops);

  TestingThreadHeapUsageTracker::DisableHeapTrackingForTesting();

  ASSERT_FALSE(ThreadHeapUsageTracker::IsHeapTrackingEnabled());
}
#endif  // BUILDFLAG(USE_ALLOCATOR_SHIM)

}  // namespace debug
}  // namespace base
