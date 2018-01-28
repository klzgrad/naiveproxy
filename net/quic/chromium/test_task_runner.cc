// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/test_task_runner.h"

#include <algorithm>
#include <utility>

#include "net/quic/test_tools/mock_clock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

TestTaskRunner::TestTaskRunner(MockClock* clock) : clock_(clock) {}

TestTaskRunner::~TestTaskRunner() {}

bool TestTaskRunner::PostDelayedTask(const base::Location& from_here,
                                     base::OnceClosure task,
                                     base::TimeDelta delay) {
  EXPECT_GE(delay, base::TimeDelta());
  tasks_.push_back(PostedTask(from_here, std::move(task), clock_->NowInTicks(),
                              delay, base::TestPendingTask::NESTABLE));
  return false;
}

bool TestTaskRunner::PostNonNestableDelayedTask(const base::Location& from_here,
                                                base::OnceClosure task,
                                                base::TimeDelta delay) {
  return PostDelayedTask(from_here, std::move(task), delay);
}

bool TestTaskRunner::RunsTasksInCurrentSequence() const {
  return true;
}

const std::vector<PostedTask>& TestTaskRunner::GetPostedTasks() const {
  return tasks_;
}

void TestTaskRunner::RunNextTask() {
  std::vector<PostedTask>::iterator next = FindNextTask();
  DCHECK(next != tasks_.end());
  clock_->AdvanceTime(QuicTime::Delta::FromMicroseconds(
      (next->GetTimeToRun() - clock_->NowInTicks()).InMicroseconds()));
  PostedTask task = std::move(*next);
  tasks_.erase(next);
  std::move(task.task).Run();
}

void TestTaskRunner::RunUntilIdle() {
  while (!tasks_.empty())
    RunNextTask();
}
namespace {

struct ShouldRunBeforeLessThan {
  bool operator()(const PostedTask& task1, const PostedTask& task2) const {
    return task1.ShouldRunBefore(task2);
  }
};

}  // namespace

std::vector<PostedTask>::iterator TestTaskRunner::FindNextTask() {
  return std::min_element(tasks_.begin(), tasks_.end(),
                          ShouldRunBeforeLessThan());
}

}  // namespace test
}  // namespace net
