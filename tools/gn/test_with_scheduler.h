// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_TEST_WITH_SCHEDULER_H_
#define TOOLS_GN_TEST_WITH_SCHEDULER_H_

#include "base/macros.h"
#include "base/test/scoped_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "tools/gn/scheduler.h"

class TestWithScheduler : public testing::Test {
 protected:
  TestWithScheduler();
  ~TestWithScheduler() override;

  Scheduler& scheduler() { return scheduler_; }

 private:
  base::test::ScopedTaskEnvironment scoped_task_environment_;
  Scheduler scheduler_;

  DISALLOW_COPY_AND_ASSIGN(TestWithScheduler);
};

#endif  // TOOLS_GN_TEST_WITH_SCHEDULER_H_
