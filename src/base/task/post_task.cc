// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/post_task.h"

#include <utility>

#include "base/logging.h"
#include "base/task/scoped_set_task_priority_for_current_thread.h"
#include "base/task/task_executor.h"
#include "base/task/thread_pool/thread_pool.h"
#include "base/task/thread_pool/thread_pool_impl.h"
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
// explicitly in |traits|, the returned TaskTraits will inherit the current
// TaskPriority.
TaskTraits GetTaskTraitsWithExplicitPriority(TaskTraits traits) {
  traits.InheritPriority(internal::GetTaskPriorityForCurrentThread());
  return traits;
}

TaskExecutor* GetTaskExecutorForTraits(const TaskTraits& traits) {
  TaskExecutor* executor = GetRegisteredTaskExecutorForTraits(traits);
  DCHECK(executor || ThreadPoolInstance::Get())
      << "Ref. Prerequisite section of post_task.h.\n\n"
         "Hint: if this is in a unit test, you're likely merely missing a "
         "base::test::ScopedTaskEnvironment member in your fixture.\n";
  // TODO(skyostil): Make thread affinity a required trait.
  if (!executor || traits.use_thread_pool())
    return static_cast<internal::ThreadPoolImpl*>(ThreadPoolInstance::Get());
  return executor;
}

}  // namespace

bool PostTask(const Location& from_here, OnceClosure task) {
  return PostDelayedTask(from_here, std::move(task), TimeDelta());
}

bool PostDelayedTask(const Location& from_here,
                     OnceClosure task,
                     TimeDelta delay) {
  return PostDelayedTaskWithTraits(from_here, TaskTraits(), std::move(task),
                                   delay);
}

bool PostTaskAndReply(const Location& from_here,
                      OnceClosure task,
                      OnceClosure reply) {
  return PostTaskWithTraitsAndReply(from_here, TaskTraits(), std::move(task),
                                    std::move(reply));
}

bool PostTaskWithTraits(const Location& from_here,
                        const TaskTraits& traits,
                        OnceClosure task) {
  return PostDelayedTaskWithTraits(from_here, traits, std::move(task),
                                   TimeDelta());
}

bool PostDelayedTaskWithTraits(const Location& from_here,
                               const TaskTraits& traits,
                               OnceClosure task,
                               TimeDelta delay) {
  const TaskTraits adjusted_traits = GetTaskTraitsWithExplicitPriority(traits);
  return GetTaskExecutorForTraits(adjusted_traits)
      ->PostDelayedTaskWithTraits(from_here, adjusted_traits, std::move(task),
                                  delay);
}

bool PostTaskWithTraitsAndReply(const Location& from_here,
                                const TaskTraits& traits,
                                OnceClosure task,
                                OnceClosure reply) {
  return PostTaskAndReplyWithTraitsTaskRunner(traits).PostTaskAndReply(
      from_here, std::move(task), std::move(reply));
}

scoped_refptr<TaskRunner> CreateTaskRunnerWithTraits(const TaskTraits& traits) {
  const TaskTraits adjusted_traits = GetTaskTraitsWithExplicitPriority(traits);
  return GetTaskExecutorForTraits(adjusted_traits)
      ->CreateTaskRunnerWithTraits(adjusted_traits);
}

scoped_refptr<SequencedTaskRunner> CreateSequencedTaskRunnerWithTraits(
    const TaskTraits& traits) {
  const TaskTraits adjusted_traits = GetTaskTraitsWithExplicitPriority(traits);
  return GetTaskExecutorForTraits(adjusted_traits)
      ->CreateSequencedTaskRunnerWithTraits(adjusted_traits);
}

scoped_refptr<UpdateableSequencedTaskRunner>
CreateUpdateableSequencedTaskRunnerWithTraits(const TaskTraits& traits) {
  DCHECK(ThreadPoolInstance::Get())
      << "Ref. Prerequisite section of post_task.h.\n\n"
         "Hint: if this is in a unit test, you're likely merely missing a "
         "base::test::ScopedTaskEnvironment member in your fixture.\n";
  DCHECK(traits.use_thread_pool())
      << "The base::UseThreadPool() trait is mandatory with "
         "CreateUpdateableSequencedTaskRunnerWithTraits().";
  CHECK_EQ(traits.extension_id(),
           TaskTraitsExtensionStorage::kInvalidExtensionId)
      << "Extension traits cannot be used with "
         "CreateUpdateableSequencedTaskRunnerWithTraits().";
  const TaskTraits adjusted_traits = GetTaskTraitsWithExplicitPriority(traits);
  return static_cast<internal::ThreadPoolImpl*>(ThreadPoolInstance::Get())
      ->CreateUpdateableSequencedTaskRunnerWithTraits(adjusted_traits);
}

scoped_refptr<SingleThreadTaskRunner> CreateSingleThreadTaskRunnerWithTraits(
    const TaskTraits& traits,
    SingleThreadTaskRunnerThreadMode thread_mode) {
  const TaskTraits adjusted_traits = GetTaskTraitsWithExplicitPriority(traits);
  return GetTaskExecutorForTraits(adjusted_traits)
      ->CreateSingleThreadTaskRunnerWithTraits(adjusted_traits, thread_mode);
}

#if defined(OS_WIN)
scoped_refptr<SingleThreadTaskRunner> CreateCOMSTATaskRunnerWithTraits(
    const TaskTraits& traits,
    SingleThreadTaskRunnerThreadMode thread_mode) {
  const TaskTraits adjusted_traits = GetTaskTraitsWithExplicitPriority(traits);
  return GetTaskExecutorForTraits(adjusted_traits)
      ->CreateCOMSTATaskRunnerWithTraits(adjusted_traits, thread_mode);
}
#endif  // defined(OS_WIN)

}  // namespace base
