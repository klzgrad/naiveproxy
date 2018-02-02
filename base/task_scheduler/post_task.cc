// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/post_task.h"

#include <utility>

#include "base/logging.h"
#include "base/task_scheduler/scoped_set_task_priority_for_current_thread.h"
#include "base/task_scheduler/task_scheduler.h"
#include "base/threading/post_task_and_reply_impl.h"

namespace base {

namespace {

class PostTaskAndReplyWithTraitsTaskRunner
    : public internal::PostTaskAndReplyImpl {
 public:
  explicit PostTaskAndReplyWithTraitsTaskRunner(const TaskTraits& traits)
      : traits_(traits) {}

 private:
  bool PostTask(const Location& from_here, OnceClosure task) override {
    PostTaskWithTraits(from_here, traits_, std::move(task));
    return true;
  }

  const TaskTraits traits_;
};

// Returns TaskTraits based on |traits|. If TaskPriority hasn't been set
// explicitly in |traits|, the returned TaskTraits have the current
// TaskPriority.
TaskTraits GetTaskTraitsWithExplicitPriority(const TaskTraits& traits) {
  if (traits.priority_set_explicitly())
    return traits;
  return TaskTraits::Override(traits,
                              {internal::GetTaskPriorityForCurrentThread()});
}

}  // namespace

void PostTask(const Location& from_here, OnceClosure task) {
  PostDelayedTask(from_here, std::move(task), TimeDelta());
}

void PostDelayedTask(const Location& from_here,
                     OnceClosure task,
                     TimeDelta delay) {
  PostDelayedTaskWithTraits(from_here, TaskTraits(), std::move(task), delay);
}

void PostTaskAndReply(const Location& from_here,
                      OnceClosure task,
                      OnceClosure reply) {
  PostTaskWithTraitsAndReply(from_here, TaskTraits(), std::move(task),
                             std::move(reply));
}

void PostTaskWithTraits(const Location& from_here,
                        const TaskTraits& traits,
                        OnceClosure task) {
  PostDelayedTaskWithTraits(from_here, traits, std::move(task), TimeDelta());
}

void PostDelayedTaskWithTraits(const Location& from_here,
                               const TaskTraits& traits,
                               OnceClosure task,
                               TimeDelta delay) {
  DCHECK(TaskScheduler::GetInstance())
      << "Ref. Prerequisite section of post_task.h.\n\n"
         "Hint: if this is in a unit test, you're likely merely missing a "
         "base::test::ScopedTaskEnvironment member in your fixture.\n";
  TaskScheduler::GetInstance()->PostDelayedTaskWithTraits(
      from_here, GetTaskTraitsWithExplicitPriority(traits), std::move(task),
      std::move(delay));
}

void PostTaskWithTraitsAndReply(const Location& from_here,
                                const TaskTraits& traits,
                                OnceClosure task,
                                OnceClosure reply) {
  PostTaskAndReplyWithTraitsTaskRunner(traits).PostTaskAndReply(
      from_here, std::move(task), std::move(reply));
}

scoped_refptr<TaskRunner> CreateTaskRunnerWithTraits(const TaskTraits& traits) {
  DCHECK(TaskScheduler::GetInstance())
      << "Ref. Prerequisite section of post_task.h.\n\n"
         "Hint: if this is in a unit test, you're likely merely missing a "
         "base::test::ScopedTaskEnvironment member in your fixture.\n";
  return TaskScheduler::GetInstance()->CreateTaskRunnerWithTraits(
      GetTaskTraitsWithExplicitPriority(traits));
}

scoped_refptr<SequencedTaskRunner> CreateSequencedTaskRunnerWithTraits(
    const TaskTraits& traits) {
  DCHECK(TaskScheduler::GetInstance())
      << "Ref. Prerequisite section of post_task.h.\n\n"
         "Hint: if this is in a unit test, you're likely merely missing a "
         "base::test::ScopedTaskEnvironment member in your fixture.\n";
  return TaskScheduler::GetInstance()->CreateSequencedTaskRunnerWithTraits(
      GetTaskTraitsWithExplicitPriority(traits));
}

scoped_refptr<SingleThreadTaskRunner> CreateSingleThreadTaskRunnerWithTraits(
    const TaskTraits& traits,
    SingleThreadTaskRunnerThreadMode thread_mode) {
  DCHECK(TaskScheduler::GetInstance())
      << "Ref. Prerequisite section of post_task.h.\n\n"
         "Hint: if this is in a unit test, you're likely merely missing a "
         "base::test::ScopedTaskEnvironment member in your fixture.\n";
  return TaskScheduler::GetInstance()->CreateSingleThreadTaskRunnerWithTraits(
      GetTaskTraitsWithExplicitPriority(traits), thread_mode);
}

#if defined(OS_WIN)
scoped_refptr<SingleThreadTaskRunner> CreateCOMSTATaskRunnerWithTraits(
    const TaskTraits& traits,
    SingleThreadTaskRunnerThreadMode thread_mode) {
  DCHECK(TaskScheduler::GetInstance())
      << "Ref. Prerequisite section of post_task.h.\n\n"
         "Hint: if this is in a unit test, you're likely merely missing a "
         "base::test::ScopedTaskEnvironment member in your fixture.\n";
  return TaskScheduler::GetInstance()->CreateCOMSTATaskRunnerWithTraits(
      GetTaskTraitsWithExplicitPriority(traits), thread_mode);
}
#endif  // defined(OS_WIN)

}  // namespace base
