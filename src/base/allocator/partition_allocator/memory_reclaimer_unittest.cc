// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/partition_allocator/memory_reclaimer.h"

#include <memory>
#include <utility>

#include "base/allocator/partition_allocator/partition_alloc.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_task_environment.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

// Otherwise, PartitionAlloc doesn't allocate any memory, and the tests are
// meaningless.
#if !defined(MEMORY_TOOL_REPLACES_ALLOCATOR)

namespace base {

class PartitionAllocMemoryReclaimerTest : public ::testing::Test {
 public:
  PartitionAllocMemoryReclaimerTest()
      : ::testing::Test(),
        task_environment_(test::ScopedTaskEnvironment::TimeSource::MOCK_TIME),
        allocator_() {}

 protected:
  void SetUp() override {
    PartitionAllocMemoryReclaimer::Instance()->ResetForTesting();
    allocator_ = std::make_unique<PartitionAllocatorGeneric>();
    allocator_->init();
  }

  void TearDown() override {
    allocator_ = nullptr;
    PartitionAllocMemoryReclaimer::Instance()->ResetForTesting();
    task_environment_.FastForwardUntilNoTasksRemain();
  }

  void StartReclaimer() {
    auto* memory_reclaimer = PartitionAllocMemoryReclaimer::Instance();
    memory_reclaimer->Start(task_environment_.GetMainThreadTaskRunner());
  }

  void AllocateAndFree() {
    void* data = allocator_->root()->Alloc(1, "");
    allocator_->root()->Free(data);
  }

  size_t GetExpectedTasksCount() const {
    // Includes the stats recording task.
    if (ElapsedThreadTimer().is_supported())
      return 2;
    return 1;
  }

  test::ScopedTaskEnvironment task_environment_;
  std::unique_ptr<PartitionAllocatorGeneric> allocator_;
};

TEST_F(PartitionAllocMemoryReclaimerTest, Simple) {
  test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      internal::kPartitionAllocPeriodicDecommit);

  StartReclaimer();

  EXPECT_EQ(GetExpectedTasksCount(),
            task_environment_.GetPendingMainThreadTaskCount());
  EXPECT_TRUE(task_environment_.NextTaskIsDelayed());
}

TEST_F(PartitionAllocMemoryReclaimerTest, IsEnabledByDefault) {
  StartReclaimer();
  EXPECT_EQ(2u, task_environment_.GetPendingMainThreadTaskCount());
}

TEST_F(PartitionAllocMemoryReclaimerTest, CanBeDisabled) {
  test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(
      internal::kPartitionAllocPeriodicDecommit);
  StartReclaimer();
  EXPECT_EQ(0u, task_environment_.GetPendingMainThreadTaskCount());
}

TEST_F(PartitionAllocMemoryReclaimerTest, FreesMemory) {
  PartitionRootGeneric* root = allocator_->root();

  size_t committed_initially = root->total_size_of_committed_pages;
  AllocateAndFree();
  size_t committed_before = root->total_size_of_committed_pages;

  EXPECT_GT(committed_before, committed_initially);

  StartReclaimer();
  task_environment_.FastForwardBy(
      task_environment_.NextMainThreadPendingTaskDelay());
  size_t committed_after = root->total_size_of_committed_pages;
  EXPECT_LT(committed_after, committed_before);
  EXPECT_LE(committed_initially, committed_after);
}

TEST_F(PartitionAllocMemoryReclaimerTest, Reclaim) {
  PartitionRootGeneric* root = allocator_->root();
  size_t committed_initially = root->total_size_of_committed_pages;

  {
    AllocateAndFree();

    size_t committed_before = root->total_size_of_committed_pages;
    EXPECT_GT(committed_before, committed_initially);
    PartitionAllocMemoryReclaimer::Instance()->Reclaim();
    size_t committed_after = root->total_size_of_committed_pages;

    EXPECT_LT(committed_after, committed_before);
    EXPECT_LE(committed_initially, committed_after);
  }
}

TEST_F(PartitionAllocMemoryReclaimerTest, DeprecatedReclaim) {
  PartitionRootGeneric* root = allocator_->root();

  // Deprecated reclaim is disabled by default.
  {
    AllocateAndFree();
    size_t committed_before = root->total_size_of_committed_pages;
    PartitionAllocMemoryReclaimer::Instance()->DeprecatedReclaim();
    size_t committed_after = root->total_size_of_committed_pages;
    EXPECT_EQ(committed_after, committed_before);

    PartitionAllocMemoryReclaimer::Instance()->Reclaim();
  }

  // Deprecated reclaim works when periodic reclaim is disabled.
  {
    test::ScopedFeatureList scoped_feature_list;
    scoped_feature_list.InitAndDisableFeature(
        internal::kPartitionAllocPeriodicDecommit);
    AllocateAndFree();
    size_t committed_before = root->total_size_of_committed_pages;
    PartitionAllocMemoryReclaimer::Instance()->DeprecatedReclaim();
    size_t committed_after = root->total_size_of_committed_pages;
    EXPECT_LT(committed_after, committed_before);
  }
}

TEST_F(PartitionAllocMemoryReclaimerTest, StatsRecording) {
  // No stats reported if the timer is not.
  if (!ElapsedThreadTimer().is_supported())
    return;

  HistogramTester histogram_tester;
  StartReclaimer();
  EXPECT_EQ(GetExpectedTasksCount(),
            task_environment_.GetPendingMainThreadTaskCount());

  task_environment_.FastForwardBy(
      PartitionAllocMemoryReclaimer::kStatsRecordingTimeDelta);
  // Hard to make sure that the total time is >1ms, so cannot assert that the
  // value is not 0.
  histogram_tester.ExpectTotalCount("Memory.PartitionAlloc.MainThreadTime.5min",
                                    1);
}

}  // namespace base
#endif  // !defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
