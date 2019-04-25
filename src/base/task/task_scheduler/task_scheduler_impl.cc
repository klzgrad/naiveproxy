// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/task_scheduler_impl.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "base/feature_list.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/field_trial_params.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/task/task_features.h"
#include "base/task/task_scheduler/scheduler_parallel_task_runner.h"
#include "base/task/task_scheduler/scheduler_sequenced_task_runner.h"
#include "base/task/task_scheduler/scheduler_worker_pool_params.h"
#include "base/task/task_scheduler/sequence.h"
#include "base/task/task_scheduler/sequence_sort_key.h"
#include "base/task/task_scheduler/service_thread.h"
#include "base/task/task_scheduler/task.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"

namespace base {
namespace internal {

namespace {

constexpr EnvironmentParams kForegroundPoolEnvironmentParams{
    "Foreground", base::ThreadPriority::NORMAL};

constexpr EnvironmentParams kBackgroundPoolEnvironmentParams{
    "Background", base::ThreadPriority::BACKGROUND};

}  // namespace

TaskSchedulerImpl::TaskSchedulerImpl(StringPiece histogram_label)
    : TaskSchedulerImpl(histogram_label,
                        std::make_unique<TaskTrackerImpl>(histogram_label)) {}

TaskSchedulerImpl::TaskSchedulerImpl(
    StringPiece histogram_label,
    std::unique_ptr<TaskTrackerImpl> task_tracker)
    : task_tracker_(std::move(task_tracker)),
      service_thread_(std::make_unique<ServiceThread>(
          task_tracker_.get(),
          BindRepeating(&TaskSchedulerImpl::ReportHeartbeatMetrics,
                        Unretained(this)))),
      delayed_task_manager_(histogram_label),
      single_thread_task_runner_manager_(task_tracker_->GetTrackedRef(),
                                         &delayed_task_manager_),
      tracked_ref_factory_(this) {
  DCHECK(!histogram_label.empty());

  foreground_pool_.emplace(
      JoinString(
          {histogram_label, kForegroundPoolEnvironmentParams.name_suffix}, "."),
      kForegroundPoolEnvironmentParams.name_suffix,
      kForegroundPoolEnvironmentParams.priority_hint,
      task_tracker_->GetTrackedRef(), tracked_ref_factory_.GetTrackedRef());

  if (CanUseBackgroundPriorityForSchedulerWorker()) {
    background_pool_.emplace(
        JoinString(
            {histogram_label, kBackgroundPoolEnvironmentParams.name_suffix},
            "."),
        kBackgroundPoolEnvironmentParams.name_suffix,
        kBackgroundPoolEnvironmentParams.priority_hint,
        task_tracker_->GetTrackedRef(), tracked_ref_factory_.GetTrackedRef());
  }
}

TaskSchedulerImpl::~TaskSchedulerImpl() {
#if DCHECK_IS_ON()
  DCHECK(join_for_testing_returned_.IsSet());
#endif

  // Reset worker pools to release held TrackedRefs, which block teardown.
  foreground_pool_.reset();
  background_pool_.reset();
}

void TaskSchedulerImpl::Start(
    const TaskScheduler::InitParams& init_params,
    SchedulerWorkerObserver* scheduler_worker_observer) {
  internal::InitializeThreadPrioritiesFeature();

  // This is set in Start() and not in the constructor because variation params
  // are usually not ready when TaskSchedulerImpl is instantiated in a process.
  if (FeatureList::IsEnabled(kAllTasksUserBlocking))
    all_tasks_user_blocking_.Set();

  // Start the service thread. On platforms that support it (POSIX except NaCL
  // SFI), the service thread runs a MessageLoopForIO which is used to support
  // FileDescriptorWatcher in the scope in which tasks run.
  ServiceThread::Options service_thread_options;
  service_thread_options.message_loop_type =
#if defined(OS_POSIX) && !defined(OS_NACL_SFI)
      MessageLoop::TYPE_IO;
#else
      MessageLoop::TYPE_DEFAULT;
#endif
  service_thread_options.timer_slack = TIMER_SLACK_MAXIMUM;
  CHECK(service_thread_->StartWithOptions(service_thread_options));

#if defined(OS_POSIX) && !defined(OS_NACL_SFI)
  // Needs to happen after starting the service thread to get its
  // task_runner().
  task_tracker_->set_io_thread_task_runner(service_thread_->task_runner());
#endif  // defined(OS_POSIX) && !defined(OS_NACL_SFI)

  // Needs to happen after starting the service thread to get its task_runner().
  scoped_refptr<TaskRunner> service_thread_task_runner =
      service_thread_->task_runner();
  delayed_task_manager_.Start(service_thread_task_runner);

  single_thread_task_runner_manager_.Start(scheduler_worker_observer);

  const SchedulerWorkerPoolImpl::WorkerEnvironment worker_environment =
#if defined(OS_WIN)
      init_params.shared_worker_pool_environment ==
              InitParams::SharedWorkerPoolEnvironment::COM_MTA
          ? SchedulerWorkerPoolImpl::WorkerEnvironment::COM_MTA
          : SchedulerWorkerPoolImpl::WorkerEnvironment::NONE;
#else
      SchedulerWorkerPoolImpl::WorkerEnvironment::NONE;
#endif

  // On platforms that can't use the background thread priority, best-effort
  // tasks run in foreground pools. A cap is set on the number of background
  // tasks that can run in foreground pools to ensure that there is always room
  // for incoming foreground tasks and to minimize the performance impact of
  // best-effort tasks.
  const int max_best_effort_tasks_in_foreground_pool = std::max(
      1, std::min(init_params.background_worker_pool_params.max_tasks(),
                  init_params.foreground_worker_pool_params.max_tasks() / 2));
  foreground_pool_->Start(init_params.foreground_worker_pool_params,
                          max_best_effort_tasks_in_foreground_pool,
                          service_thread_task_runner, scheduler_worker_observer,
                          worker_environment);

  if (background_pool_.has_value()) {
    background_pool_->Start(
        init_params.background_worker_pool_params,
        init_params.background_worker_pool_params.max_tasks(),
        service_thread_task_runner, scheduler_worker_observer,
        worker_environment);
  }
}

bool TaskSchedulerImpl::PostDelayedTaskWithTraits(const Location& from_here,
                                                  const TaskTraits& traits,
                                                  OnceClosure task,
                                                  TimeDelta delay) {
  // Post |task| as part of a one-off single-task Sequence.
  const TaskTraits new_traits = SetUserBlockingPriorityIfNeeded(traits);
  return PostTaskWithSequence(Task(from_here, std::move(task), delay),
                              MakeRefCounted<Sequence>(new_traits));
}

scoped_refptr<TaskRunner> TaskSchedulerImpl::CreateTaskRunnerWithTraits(
    const TaskTraits& traits) {
  const TaskTraits new_traits = SetUserBlockingPriorityIfNeeded(traits);
  return MakeRefCounted<SchedulerParallelTaskRunner>(new_traits, this);
}

scoped_refptr<SequencedTaskRunner>
TaskSchedulerImpl::CreateSequencedTaskRunnerWithTraits(
    const TaskTraits& traits) {
  const TaskTraits new_traits = SetUserBlockingPriorityIfNeeded(traits);
  return MakeRefCounted<SchedulerSequencedTaskRunner>(new_traits, this);
}

scoped_refptr<SingleThreadTaskRunner>
TaskSchedulerImpl::CreateSingleThreadTaskRunnerWithTraits(
    const TaskTraits& traits,
    SingleThreadTaskRunnerThreadMode thread_mode) {
  return single_thread_task_runner_manager_
      .CreateSingleThreadTaskRunnerWithTraits(
          SetUserBlockingPriorityIfNeeded(traits), thread_mode);
}

#if defined(OS_WIN)
scoped_refptr<SingleThreadTaskRunner>
TaskSchedulerImpl::CreateCOMSTATaskRunnerWithTraits(
    const TaskTraits& traits,
    SingleThreadTaskRunnerThreadMode thread_mode) {
  return single_thread_task_runner_manager_.CreateCOMSTATaskRunnerWithTraits(
      SetUserBlockingPriorityIfNeeded(traits), thread_mode);
}
#endif  // defined(OS_WIN)

scoped_refptr<UpdateableSequencedTaskRunner>
TaskSchedulerImpl::CreateUpdateableSequencedTaskRunnerWithTraitsForTesting(
    const TaskTraits& traits) {
  const TaskTraits new_traits = SetUserBlockingPriorityIfNeeded(traits);
  return MakeRefCounted<SchedulerSequencedTaskRunner>(new_traits, this);
}

int TaskSchedulerImpl::GetMaxConcurrentNonBlockedTasksWithTraitsDeprecated(
    const TaskTraits& traits) const {
  // This method does not support getting the maximum number of BEST_EFFORT
  // tasks that can run concurrently in a pool.
  DCHECK_NE(traits.priority(), TaskPriority::BEST_EFFORT);
  return GetWorkerPoolForTraits(traits)
      ->GetMaxConcurrentNonBlockedTasksDeprecated();
}

void TaskSchedulerImpl::Shutdown() {
  task_tracker_->Shutdown();
}

void TaskSchedulerImpl::FlushForTesting() {
  task_tracker_->FlushForTesting();
}

void TaskSchedulerImpl::FlushAsyncForTesting(OnceClosure flush_callback) {
  task_tracker_->FlushAsyncForTesting(std::move(flush_callback));
}

void TaskSchedulerImpl::JoinForTesting() {
#if DCHECK_IS_ON()
  DCHECK(!join_for_testing_returned_.IsSet());
#endif
  // The service thread must be stopped before the workers are joined, otherwise
  // tasks scheduled by the DelayedTaskManager might be posted between joining
  // those workers and stopping the service thread which will cause a CHECK. See
  // https://crbug.com/771701.
  service_thread_->Stop();
  single_thread_task_runner_manager_.JoinForTesting();
  foreground_pool_->JoinForTesting();
  if (background_pool_.has_value())
    background_pool_->JoinForTesting();
#if DCHECK_IS_ON()
  join_for_testing_returned_.Set();
#endif
}

void TaskSchedulerImpl::SetExecutionFenceEnabled(bool execution_fence_enabled) {
  task_tracker_->SetExecutionFenceEnabled(execution_fence_enabled);
}

bool TaskSchedulerImpl::PostTaskWithSequence(Task task,
                                             scoped_refptr<Sequence> sequence) {
  // Use CHECK instead of DCHECK to crash earlier. See http://crbug.com/711167
  // for details.
  CHECK(task.task);
  DCHECK(sequence);

  if (!task_tracker_->WillPostTask(&task, sequence->shutdown_behavior()))
    return false;

  if (task.delayed_run_time.is_null()) {
    auto sequence_and_transaction =
        SequenceAndTransaction::FromSequence(std::move(sequence));
    const TaskTraits traits = sequence_and_transaction.transaction.traits();
    GetWorkerPoolForTraits(traits)->PostTaskWithSequenceNow(
        std::move(task), std::move(sequence_and_transaction));
  } else {
    delayed_task_manager_.AddDelayedTask(
        std::move(task),
        BindOnce(
            [](scoped_refptr<Sequence> sequence,
               TaskSchedulerImpl* task_scheduler_impl, Task task) {
              auto sequence_and_transaction =
                  SequenceAndTransaction::FromSequence(std::move(sequence));
              const TaskTraits traits =
                  sequence_and_transaction.transaction.traits();
              task_scheduler_impl->GetWorkerPoolForTraits(traits)
                  ->PostTaskWithSequenceNow(
                      std::move(task), std::move(sequence_and_transaction));
            },
            std::move(sequence), Unretained(this)));
  }

  return true;
}

bool TaskSchedulerImpl::IsRunningPoolWithTraits(
    const TaskTraits& traits) const {
  return GetWorkerPoolForTraits(traits)->IsBoundToCurrentThread();
}

void TaskSchedulerImpl::UpdatePriority(scoped_refptr<Sequence> sequence,
                                       TaskPriority priority) {
  auto sequence_and_transaction =
      SequenceAndTransaction::FromSequence(std::move(sequence));

  SchedulerWorkerPool* const current_worker_pool =
      GetWorkerPoolForTraits(sequence_and_transaction.transaction.traits());
  sequence_and_transaction.transaction.UpdatePriority(priority);
  SchedulerWorkerPool* const new_worker_pool =
      GetWorkerPoolForTraits(sequence_and_transaction.transaction.traits());

  if (new_worker_pool == current_worker_pool) {
    // |sequence|'s position needs to be updated within its current pool.
    current_worker_pool->UpdateSortKey(std::move(sequence_and_transaction));
  } else {
    // |sequence| is changing pools; remove it from its current pool and
    // reenqueue it.
    const bool sequence_was_found =
        current_worker_pool->RemoveSequence(sequence_and_transaction.sequence);
    if (sequence_was_found) {
      DCHECK(sequence_and_transaction.sequence);
      new_worker_pool->ReEnqueueSequenceChangingPool(
          std::move(sequence_and_transaction));
    }
  }
}

const SchedulerWorkerPool* TaskSchedulerImpl::GetWorkerPoolForTraits(
    const TaskTraits& traits) const {
  return const_cast<TaskSchedulerImpl*>(this)->GetWorkerPoolForTraits(traits);
}

SchedulerWorkerPool* TaskSchedulerImpl::GetWorkerPoolForTraits(
    const TaskTraits& traits) {
  if (traits.priority() == TaskPriority::BEST_EFFORT &&
      background_pool_.has_value()) {
    return &background_pool_.value();
  }
  return &foreground_pool_.value();
}

TaskTraits TaskSchedulerImpl::SetUserBlockingPriorityIfNeeded(
    const TaskTraits& traits) const {
  return all_tasks_user_blocking_.IsSet()
             ? TaskTraits::Override(traits, {TaskPriority::USER_BLOCKING})
             : traits;
}

void TaskSchedulerImpl::ReportHeartbeatMetrics() const {
  foreground_pool_->ReportHeartbeatMetrics();
  if (background_pool_.has_value())
    background_pool_->ReportHeartbeatMetrics();
}

}  // namespace internal
}  // namespace base
