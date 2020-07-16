// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/thread_task_runner_handle.h"

#include "base/memory/ref_counted.h"
#include "base/test/gtest_util.h"
#include "base/test/test_simple_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

TEST(ThreadTaskRunnerHandleTest, Basic) {
  scoped_refptr<SingleThreadTaskRunner> task_runner(new TestSimpleTaskRunner);

  EXPECT_FALSE(ThreadTaskRunnerHandle::IsSet());
  {
    ThreadTaskRunnerHandle ttrh1(task_runner);
    EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
    EXPECT_EQ(task_runner, ThreadTaskRunnerHandle::Get());
  }
  EXPECT_FALSE(ThreadTaskRunnerHandle::IsSet());
}

TEST(ThreadTaskRunnerHandleTest, DeathOnImplicitOverride) {
  scoped_refptr<SingleThreadTaskRunner> task_runner(new TestSimpleTaskRunner);
  scoped_refptr<SingleThreadTaskRunner> overidding_task_runner(
      new TestSimpleTaskRunner);

  ThreadTaskRunnerHandle ttrh(task_runner);
  EXPECT_DCHECK_DEATH(
      { ThreadTaskRunnerHandle overriding_ttrh(overidding_task_runner); });
}

TEST(ThreadTaskRunnerHandleTest, OverrideForTestingExistingTTRH) {
  scoped_refptr<SingleThreadTaskRunner> task_runner_1(new TestSimpleTaskRunner);
  scoped_refptr<SingleThreadTaskRunner> task_runner_2(new TestSimpleTaskRunner);
  scoped_refptr<SingleThreadTaskRunner> task_runner_3(new TestSimpleTaskRunner);
  scoped_refptr<SingleThreadTaskRunner> task_runner_4(new TestSimpleTaskRunner);

  EXPECT_FALSE(ThreadTaskRunnerHandle::IsSet());
  {
    // TTRH in place prior to override.
    ThreadTaskRunnerHandle ttrh1(task_runner_1);
    EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
    EXPECT_EQ(task_runner_1, ThreadTaskRunnerHandle::Get());

    {
      // Override.
      ScopedClosureRunner undo_override_2 =
          ThreadTaskRunnerHandle::OverrideForTesting(task_runner_2);
      EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
      EXPECT_EQ(task_runner_2, ThreadTaskRunnerHandle::Get());

      {
        // Nested override.
        ScopedClosureRunner undo_override_3 =
            ThreadTaskRunnerHandle::OverrideForTesting(task_runner_3);
        EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
        EXPECT_EQ(task_runner_3, ThreadTaskRunnerHandle::Get());
      }

      // Back to single override.
      EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
      EXPECT_EQ(task_runner_2, ThreadTaskRunnerHandle::Get());

      {
        // Backup to double override with another TTRH.
        ScopedClosureRunner undo_override_4 =
            ThreadTaskRunnerHandle::OverrideForTesting(task_runner_4);
        EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
        EXPECT_EQ(task_runner_4, ThreadTaskRunnerHandle::Get());
      }
    }

    // Back to simple TTRH.
    EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
    EXPECT_EQ(task_runner_1, ThreadTaskRunnerHandle::Get());
  }
  EXPECT_FALSE(ThreadTaskRunnerHandle::IsSet());
}

TEST(ThreadTaskRunnerHandleTest, OverrideForTestingNoExistingTTRH) {
  scoped_refptr<SingleThreadTaskRunner> task_runner_1(new TestSimpleTaskRunner);
  scoped_refptr<SingleThreadTaskRunner> task_runner_2(new TestSimpleTaskRunner);

  EXPECT_FALSE(ThreadTaskRunnerHandle::IsSet());
  {
    // Override with no TTRH in place.
    ScopedClosureRunner undo_override_1 =
        ThreadTaskRunnerHandle::OverrideForTesting(task_runner_1);
    EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
    EXPECT_EQ(task_runner_1, ThreadTaskRunnerHandle::Get());

    {
      // Nested override works the same.
      ScopedClosureRunner undo_override_2 =
          ThreadTaskRunnerHandle::OverrideForTesting(task_runner_2);
      EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
      EXPECT_EQ(task_runner_2, ThreadTaskRunnerHandle::Get());
    }

    // Back to single override.
    EXPECT_TRUE(ThreadTaskRunnerHandle::IsSet());
    EXPECT_EQ(task_runner_1, ThreadTaskRunnerHandle::Get());
  }
  EXPECT_FALSE(ThreadTaskRunnerHandle::IsSet());
}

TEST(ThreadTaskRunnerHandleTest, DeathOnTTRHOverOverride) {
  scoped_refptr<SingleThreadTaskRunner> task_runner(new TestSimpleTaskRunner);
  scoped_refptr<SingleThreadTaskRunner> overidding_task_runner(
      new TestSimpleTaskRunner);

  ScopedClosureRunner undo_override =
      ThreadTaskRunnerHandle::OverrideForTesting(task_runner);
  EXPECT_DCHECK_DEATH(
      { ThreadTaskRunnerHandle overriding_ttrh(overidding_task_runner); });
}

}  // namespace base
