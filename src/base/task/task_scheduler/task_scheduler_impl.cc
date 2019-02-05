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

// Returns worker pool EnvironmentType for given arguments |is_background| and
// |is_blocking|.
EnvironmentType GetEnvironmentIndex(bool is_background, bool is_blocking) {
  if (is_background) {
    if (is_blocking)
      return BACKGROUND_BLOCKING;
    return BACKGROUND;
  }

  if (is_blocking)
    return FOREGROUND_BLOCKING;
  return FOREGROUND;
}

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
      single_thread_task_runner_manager_(task_tracker_->GetTrackedRef(),
                                         &delayed_task_manager_),
      tracked_ref_factory_(this) {
  DCHECK(!histogram_label.empty());

  static_assert(arraysize(environment_to_worker_pool_) == ENVIRONMENT_COUNT,
                "The size of |environment_to_worker_pool_| must match "
                "ENVIRONMENT_COUNT.");
  static_assert(
      size(kEnvironmentParams) == ENVIRONMENT_COUNT,
      "The size of |kEnvironmentParams| must match ENVIRONMENT_COUNT.");

  int num_pools_to_create = CanUseBackgroundPriorityForSchedulerWorker()
                                ? ENVIRONMENT_COUNT
                                : ENVIRONMENT_COUNT_WITHOUT_BACKGROUND_PRIORITY;
  for (int environment_type = 0; environment_type < num_pools_to_create;
       ++environment_type) {
    worker_pools_.emplace_back(std::make_unique<SchedulerWorkerPoolImpl>(
        JoinString(
            {histogram_label, kEnvironmentParams[environment_type].name_suffix},
            "."),
        kEnvironmentParams[environment_type].name_suffix,
        kEnvironmentParams[environment_type].priority_hint,
        task_tracker_->GetTrackedRef(), tracked_ref_factory_.GetTrackedRef()));
  }

  // Map environment indexes to pools. |kMergeBlockingNonBlockingPools| is
  // assumed to be disabled.
  environment_to_worker_pool_[FOREGROUND] =
      worker_pools_[GetEnvironmentIndex(false, false)].get();
  environment_to_worker_pool_[FOREGROUND_BLOCKING] =
      worker_pools_[GetEnvironmentIndex(false, true)].get();
  environment_to_worker_pool_[BACKGROUND] =
      worker_pools_[GetEnvironmentIndex(
                        CanUseBackgroundPriorityForSchedulerWorker(), false)]
          .get();
  environment_to_worker_pool_[BACKGROUND_BLOCKING] =
      worker_pools_[GetEnvironmentIndex(
                        CanUseBackgroundPriorityForSchedulerWorker(), true)]
          .get();
}

TaskSchedulerImpl::~TaskSchedulerImpl() {
#if DCHECK_IS_ON()
  DCHECK(join_for_testing_returned_.IsSet());
#endif

  // Clear |worker_pools_| to release held TrackedRefs, which block teardown.
  worker_pools_.clear();
}

void TaskSchedulerImpl::Start(
    const TaskScheduler::InitParams& init_params,
    SchedulerWorkerObserver* scheduler_worker_observer) {
  internal::InitializeThreadPrioritiesFeature();

  // This is set in Start() and not in the constructor because variation params
  // are usually not ready when TaskSchedulerImpl is instantiated in a process.
  if (FeatureList::IsEnabled(kAllTasksUserBlocking))
    all_tasks_user_blocking_.Set();

  const bool use_blocking_pools =
      !base::FeatureList::IsEnabled(kMergeBlockingNonBlockingPools);

  // Remap environment indexes to pools with |use_blocking_pools|.
  // TODO(etiennep): This is only necessary because of the kMergeBlockingNonBlockingPools
  // experiment. Remove this after the experiment.
  environment_to_worker_pool_[FOREGROUND] =
      worker_pools_[GetEnvironmentIndex(false, false)].get();
  environment_to_worker_pool_[FOREGROUND_BLOCKING] =
      worker_pools_[GetEnvironmentIndex(false, use_blocking_pools)].get();
  environment_to_worker_pool_[BACKGROUND] =
      worker_pools_[GetEnvironmentIndex(
                        CanUseBackgroundPriorityForSchedulerWorker(), false)]
          .get();
  environment_to_worker_pool_[BACKGROUND_BLOCKING] =
      worker_pools_[GetEnvironmentIndex(
                        CanUseBackgroundPriorityForSchedulerWorker(),
                        use_blocking_pools)]
          .get();

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
  worker_pools_[FOREGROUND]->Start(
      init_params.foreground_worker_pool_params,
      max_best_effort_tasks_in_foreground_pool, service_thread_task_runner,
      scheduler_worker_observer, worker_environment);
  const int max_best_effort_tasks_in_foreground_blocking_pool = std::max(
      1, std::min(
             init_params.background_blocking_worker_pool_params.max_tasks(),
             init_params.foreground_blocking_worker_pool_params.max_tasks() /
                 2));
  worker_pools_[FOREGROUND_BLOCKING]->Start(
      init_params.foreground_blocking_worker_pool_params,
      max_best_effort_tasks_in_foreground_blocking_pool,
      service_thread_task_runner, scheduler_worker_observer,
      worker_environment);

  if (CanUseBackgroundPriorityForSchedulerWorker()) {
    worker_pools_[BACKGROUND]->Start(
        init_params.background_worker_pool_params,
        init_params.background_worker_pool_params.max_tasks(),
        service_thread_task_runner, scheduler_worker_observer,
        worker_environment);
    worker_pools_[BACKGROUND_BLOCKING]->Start(
        init_params.background_blocking_worker_pool_params,
        init_params.background_blocking_worker_pool_params.max_tasks(),
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

std::vector<const HistogramBase*> TaskSchedulerImpl::GetHistograms() const {
  std::vector<const HistogramBase*> histograms;
  for (const auto& worker_pool : worker_pools_)
    worker_pool->GetHistograms(&histograms);

  return histograms;
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
  for (const auto& worker_pool : worker_pools_)
    worker_pool->JoinForTesting();
#if DCHECK_IS_ON()
  join_for_testing_returned_.Set();
#endif
}

void TaskSchedulerImpl::SetExecutionFenceEnabled(bool execution_fence_enabled) {
  task_tracker_->SetExecutionFenceEnabled(execution_fence_enabled);
}

void TaskSchedulerImpl::ReEnqueueSequence(
    SequenceAndTransaction sequence_and_transaction) {
  const TaskTraits new_traits = SetUserBlockingPriorityIfNeeded(
      sequence_and_transaction.transaction.traits());
  SchedulerWorkerPool* const destination_worker_pool =
      GetWorkerPoolForTraits(new_traits);
  const bool is_changing_pools =
      !destination_worker_pool->IsBoundToCurrentThread();
  destination_worker_pool->ReEnqueueSequence(
      std::move(sequence_and_transaction), is_changing_pools);
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

  SchedulerWorkerPoolImpl* const current_worker_pool =
      GetWorkerPoolForTraits(sequence_and_transaction.transaction.traits());
  sequence_and_transaction.transaction.UpdatePriority(priority);
  SchedulerWorkerPoolImpl* const new_worker_pool =
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
      // |sequence| was removed from |current_worker_pool| and is being
      // reenqueued into |new_worker_pool|, a different pool; set argument
      // |is_changing_pools| to true to notify |new_worker_pool| that
      // |sequence| came from a different pool.
      const bool is_changing_pools = true;
      new_worker_pool->ReEnqueueSequence(std::move(sequence_and_transaction),
                                         is_changing_pools);
    }
  }
}

SchedulerWorkerPoolImpl* TaskSchedulerImpl::GetWorkerPoolForTraits(
    const TaskTraits& traits) const {
  return environment_to_worker_pool_[GetEnvironmentIndexForTraits(traits)];
}

TaskTraits TaskSchedulerImpl::SetUserBlockingPriorityIfNeeded(
    const TaskTraits& traits) const {
  return all_tasks_user_blocking_.IsSet()
             ? TaskTraits::Override(traits, {TaskPriority::USER_BLOCKING})
             : traits;
}

void TaskSchedulerImpl::ReportHeartbeatMetrics() const {
  for (const auto& worker_pool : worker_pools_)
    worker_pool->RecordNumWorkersHistogram();
}

}  // namespace internal
}  // namespace base
