// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Common utilities for Quic tests

#ifndef NET_QUIC_TEST_TOOLS_TEST_TASK_RUNNER_H_
#define NET_QUIC_TEST_TOOLS_TEST_TASK_RUNNER_H_

#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/task_runner.h"
#include "base/test/test_pending_task.h"

namespace net {

class MockClock;

namespace test {

typedef base::TestPendingTask PostedTask;

class TestTaskRunner : public base::SequencedTaskRunner {
 public:
  explicit TestTaskRunner(MockClock* clock);

  // base::TaskRunner implementation.
  bool PostDelayedTask(const base::Location& from_here,
                       base::OnceClosure task,
                       base::TimeDelta delay) override;
  bool PostNonNestableDelayedTask(const base::Location& from_here,
                                  base::OnceClosure task,
                                  base::TimeDelta delay) override;

  bool RunsTasksInCurrentSequence() const override;

  const std::vector<PostedTask>& GetPostedTasks() const;

  // Finds the next task to run, advances the time to the correct time
  // and then runs the task.
  void RunNextTask();

  // While there are posted tasks, finds the next task to run, advances the
  // time to the correct time and then runs the task.
  void RunUntilIdle();

 protected:
  ~TestTaskRunner() override;

 private:
  std::vector<PostedTask>::iterator FindNextTask();

  MockClock* const clock_;
  std::vector<PostedTask> tasks_;

  DISALLOW_COPY_AND_ASSIGN(TestTaskRunner);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_TEST_TASK_RUNNER_H_
