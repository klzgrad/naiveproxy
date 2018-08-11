// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/task_scheduler_impl.h"

#include <string>
#include <utility>

#include "base/compiler_specific.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/field_trial_params.h"
#include "base/strings/string_util.h"
#include "base/task_scheduler/delayed_task_manager.h"
#include "base/task_scheduler/environment_config.h"
#include "base/task_scheduler/scheduler_worker_pool_params.h"
#include "base/task_scheduler/sequence.h"
#include "base/task_scheduler/sequence_sort_key.h"
#include "base/task_scheduler/service_thread.h"
#include "base/task_scheduler/task.h"
#include "base/task_scheduler/task_tracker.h"
#include "base/time/time.h"

namespace base {
namespace internal {

TaskSchedulerImpl::TaskSchedulerImpl(StringPiece histogram_label)
    : TaskSchedulerImpl(histogram_label,
                        std::make_unique<TaskTrackerImpl>(histogram_label)) {}

TaskSchedulerImpl::TaskSchedulerImpl(
    StringPiece histogram_label,
    std::unique_ptr<TaskTrackerImpl> task_tracker)
    : task_tracker_(std::move(task_tracker)),
      service_thread_(std::make_unique<ServiceThread>(task_tracker_.get())),
      single_thread_task_runner_manager_(task_tracker_->GetTrackedRef(),
                                         &delayed_task_manager_) {
  DCHECK(!histogram_label.empty());

  static_assert(arraysize(worker_pools_) == ENVIRONMENT_COUNT,
                "The size of |worker_pools_| must match ENVIRONMENT_COUNT.");
  static_assert(
      arraysize(kEnvironmentParams) == ENVIRONMENT_COUNT,
      "The size of |kEnvironmentParams| must match ENVIRONMENT_COUNT.");

  for (int environment_type = 0; environment_type < ENVIRONMENT_COUNT;
       ++environment_type) {
    worker_pools_[environment_type] = std::make_unique<SchedulerWorkerPoolImpl>(
        JoinString(
            {histogram_label, kEnvironmentParams[environment_type].name_suffix},
            "."),
        kEnvironmentParams[environment_type].name_suffix,
        kEnvironmentParams[environment_type].priority_hint,
        task_tracker_->GetTrackedRef(), &delayed_task_manager_);
  }
}

TaskSchedulerImpl::~TaskSchedulerImpl() {
#if DCHECK_IS_ON()
  DCHECK(join_for_testing_returned_.IsSet());
#endif
}

void TaskSchedulerImpl::Start(
    const TaskScheduler::InitParams& init_params,
    SchedulerWorkerObserver* scheduler_worker_observer) {
  // This is set in Start() and not in the constructor because variation params
  // are usually not ready when TaskSchedulerImpl is instantiated in a process.
  if (base::GetFieldTrialParamValue("BrowserScheduler",
                                    "AllTasksUserBlocking") == "true") {
    all_tasks_user_blocking_.Set();
  }

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
  // message_loop().
  task_tracker_->set_watch_file_descriptor_message_loop(
      static_cast<MessageLoopForIO*>(service_thread_->message_loop()));

#if DCHECK_IS_ON()
  task_tracker_->set_service_thread_handle(service_thread_->GetThreadHandle());
#endif  // DCHECK_IS_ON()
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

  worker_pools_[BACKGROUND]->Start(
      init_params.background_worker_pool_params, service_thread_task_runner,
      scheduler_worker_observer, worker_environment);
  worker_pools_[BACKGROUND_BLOCKING]->Start(
      init_params.background_blocking_worker_pool_params,
      service_thread_task_runner, scheduler_worker_observer,
      worker_environment);
  worker_pools_[FOREGROUND]->Start(
      init_params.foreground_worker_pool_params, service_thread_task_runner,
      scheduler_worker_observer, worker_environment);
  worker_pools_[FOREGROUND_BLOCKING]->Start(
      init_params.foreground_blocking_worker_pool_params,
      service_thread_task_runner, scheduler_worker_observer,
      worker_environment);
}

void TaskSchedulerImpl::PostDelayedTaskWithTraits(const Location& from_here,
                                                  const TaskTraits& traits,
                                                  OnceClosure task,
                                                  TimeDelta delay) {
  // Post |task| as part of a one-off single-task Sequence.
  const TaskTraits new_traits = SetUserBlockingPriorityIfNeeded(traits);
  GetWorkerPoolForTraits(new_traits)
      ->PostTaskWithSequence(
          Task(from_here, std::move(task), new_traits, delay),
          MakeRefCounted<Sequence>());
}

scoped_refptr<TaskRunner> TaskSchedulerImpl::CreateTaskRunnerWithTraits(
    const TaskTraits& traits) {
  const TaskTraits new_traits = SetUserBlockingPriorityIfNeeded(traits);
  return GetWorkerPoolForTraits(new_traits)
      ->CreateTaskRunnerWithTraits(new_traits);
}

scoped_refptr<SequencedTaskRunner>
TaskSchedulerImpl::CreateSequencedTaskRunnerWithTraits(
    const TaskTraits& traits) {
  const TaskTraits new_traits = SetUserBlockingPriorityIfNeeded(traits);
  return GetWorkerPoolForTraits(new_traits)
      ->CreateSequencedTaskRunnerWithTraits(new_traits);
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
  return GetWorkerPoolForTraits(traits)
      ->GetMaxConcurrentNonBlockedTasksDeprecated();
}

void TaskSchedulerImpl::Shutdown() {
  // TODO(fdoray): Increase the priority of BACKGROUND tasks blocking shutdown.
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

SchedulerWorkerPoolImpl* TaskSchedulerImpl::GetWorkerPoolForTraits(
    const TaskTraits& traits) const {
  return worker_pools_[GetEnvironmentIndexForTraits(traits)].get();
}

TaskTraits TaskSchedulerImpl::SetUserBlockingPriorityIfNeeded(
    const TaskTraits& traits) const {
  return all_tasks_user_blocking_.IsSet()
             ? TaskTraits::Override(traits, {TaskPriority::USER_BLOCKING})
             : traits;
}

}  // namespace internal
}  // namespace base
