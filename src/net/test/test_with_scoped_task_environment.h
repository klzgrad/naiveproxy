// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_TEST_WITH_SCOPED_TASK_ENVIRONMENT_H_
#define NET_TEST_TEST_WITH_SCOPED_TASK_ENVIRONMENT_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/test/scoped_task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
class TickClock;
}  // namespace base

namespace net {

// Inherit from this class if a ScopedTaskEnvironment is needed in a test.
// Use in class hierachies where inheritance from ::testing::Test at the same
// time is not desirable or possible (for example, when inheriting from
// PlatformTest at the same time).
class WithScopedTaskEnvironment {
 protected:
  // Always uses MainThreadType::IO, |time_source| may optionally be provided
  // to mock time.
  explicit WithScopedTaskEnvironment(
      base::test::ScopedTaskEnvironment::TimeSource time_source =
          base::test::ScopedTaskEnvironment::TimeSource::DEFAULT)
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::IO,
            time_source) {}

  bool MainThreadIsIdle() const WARN_UNUSED_RESULT {
    return scoped_task_environment_.MainThreadIsIdle();
  }

  void RunUntilIdle() { scoped_task_environment_.RunUntilIdle(); }

  void FastForwardBy(base::TimeDelta delta) {
    scoped_task_environment_.FastForwardBy(delta);
  }

  void FastForwardUntilNoTasksRemain() {
    scoped_task_environment_.FastForwardUntilNoTasksRemain();
  }

  const base::TickClock* GetMockTickClock() WARN_UNUSED_RESULT {
    return scoped_task_environment_.GetMockTickClock();
  }

  size_t GetPendingMainThreadTaskCount() const WARN_UNUSED_RESULT {
    return scoped_task_environment_.GetPendingMainThreadTaskCount();
  }

  base::TimeDelta NextMainThreadPendingTaskDelay() const WARN_UNUSED_RESULT {
    return scoped_task_environment_.NextMainThreadPendingTaskDelay();
  }

 private:
  base::test::ScopedTaskEnvironment scoped_task_environment_;

  DISALLOW_COPY_AND_ASSIGN(WithScopedTaskEnvironment);
};

// Inherit from this class instead of ::testing::Test directly if a
// ScopedTaskEnvironment is needed in a test.
class TestWithScopedTaskEnvironment : public ::testing::Test,
                                      public WithScopedTaskEnvironment {
 protected:
  using WithScopedTaskEnvironment::WithScopedTaskEnvironment;

  DISALLOW_COPY_AND_ASSIGN(TestWithScopedTaskEnvironment);
};

}  // namespace net

#endif  // NET_TEST_TEST_WITH_SCOPED_TASK_ENVIRONMENT_H_
