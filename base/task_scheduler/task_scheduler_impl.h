// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_TASK_SCHEDULER_IMPL_H_
#define BASE_TASK_SCHEDULER_TASK_SCHEDULER_IMPL_H_

#include <memory>
#include <vector>

#include "base/base_export.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_piece.h"
#include "base/synchronization/atomic_flag.h"
#include "base/task_scheduler/delayed_task_manager.h"
#include "base/task_scheduler/scheduler_single_thread_task_runner_manager.h"
#include "base/task_scheduler/scheduler_worker_pool_impl.h"
#include "base/task_scheduler/single_thread_task_runner_thread_mode.h"
#include "base/task_scheduler/task_scheduler.h"
#include "base/task_scheduler/task_tracker.h"
#include "base/task_scheduler/task_traits.h"
#include "base/threading/thread.h"
#include "build/build_config.h"

#if defined(OS_POSIX) && !defined(OS_NACL_SFI)
#include "base/task_scheduler/task_tracker_posix.h"
#endif

#if defined(OS_WIN)
#include "base/win/com_init_check_hook.h"
#endif

namespace base {

class HistogramBase;

namespace internal {

// Default TaskScheduler implementation. This class is thread-safe.
class BASE_EXPORT TaskSchedulerImpl : public TaskScheduler {
 public:
  using TaskTrackerImpl =
#if defined(OS_POSIX) && !defined(OS_NACL_SFI)
      TaskTrackerPosix;
#else
      TaskTracker;
#endif

  // |name| is used to label threads and histograms. |task_tracker| can be used
  // for tests that need more execution control. By default, the production
  // TaskTracker is used.
  explicit TaskSchedulerImpl(StringPiece name,
                             std::unique_ptr<TaskTrackerImpl> task_tracker =
                                 std::make_unique<TaskTrackerImpl>());
  ~TaskSchedulerImpl() override;

  // TaskScheduler:
  void Start(const TaskScheduler::InitParams& init_params) override;
  void PostDelayedTaskWithTraits(const Location& from_here,
                                 const TaskTraits& traits,
                                 OnceClosure task,
                                 TimeDelta delay) override;
  scoped_refptr<TaskRunner> CreateTaskRunnerWithTraits(
      const TaskTraits& traits) override;
  scoped_refptr<SequencedTaskRunner> CreateSequencedTaskRunnerWithTraits(
      const TaskTraits& traits) override;
  scoped_refptr<SingleThreadTaskRunner> CreateSingleThreadTaskRunnerWithTraits(
      const TaskTraits& traits,
      SingleThreadTaskRunnerThreadMode thread_mode) override;
#if defined(OS_WIN)
  scoped_refptr<SingleThreadTaskRunner> CreateCOMSTATaskRunnerWithTraits(
      const TaskTraits& traits,
      SingleThreadTaskRunnerThreadMode thread_mode) override;
#endif  // defined(OS_WIN)
  std::vector<const HistogramBase*> GetHistograms() const override;
  int GetMaxConcurrentNonBlockedTasksWithTraitsDeprecated(
      const TaskTraits& traits) const override;
  void Shutdown() override;
  void FlushForTesting() override;
  void JoinForTesting() override;

 private:
  // Returns the worker pool that runs Tasks with |traits|.
  SchedulerWorkerPoolImpl* GetWorkerPoolForTraits(
      const TaskTraits& traits) const;

  // Returns |traits|, with priority set to TaskPriority::USER_BLOCKING if
  // |all_tasks_user_blocking_| is set.
  TaskTraits SetUserBlockingPriorityIfNeeded(const TaskTraits& traits) const;

  const std::string name_;
  Thread service_thread_;
  const std::unique_ptr<TaskTrackerImpl> task_tracker_;
  DelayedTaskManager delayed_task_manager_;
  SchedulerSingleThreadTaskRunnerManager single_thread_task_runner_manager_;

  // Indicates that all tasks are handled as if they had been posted with
  // TaskPriority::USER_BLOCKING. Since this is set in Start(), it doesn't apply
  // to tasks posted before Start() or to tasks posted to TaskRunners created
  // before Start().
  //
  // TODO(fdoray): Remove after experiment. https://crbug.com/757022
  AtomicFlag all_tasks_user_blocking_;

  // There are 4 SchedulerWorkerPoolImpl in this array to match the 4
  // SchedulerWorkerPoolParams in TaskScheduler::InitParams.
  std::unique_ptr<SchedulerWorkerPoolImpl> worker_pools_[4];

#if DCHECK_IS_ON()
  // Set once JoinForTesting() has returned.
  AtomicFlag join_for_testing_returned_;
#endif

#if defined(OS_WIN) && defined(COM_INIT_CHECK_HOOK_ENABLED)
  // Provides COM initialization verification for supported builds.
  base::win::ComInitCheckHook com_init_check_hook_;
#endif

  DISALLOW_COPY_AND_ASSIGN(TaskSchedulerImpl);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_SCHEDULER_TASK_SCHEDULER_IMPL_H_
