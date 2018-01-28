// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_SCHEDULER_SINGLE_THREAD_TASK_RUNNER_MANAGER_H_
#define BASE_TASK_SCHEDULER_SCHEDULER_SINGLE_THREAD_TASK_RUNNER_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/task_scheduler/environment_config.h"
#include "base/task_scheduler/scheduler_lock.h"
#include "base/task_scheduler/single_thread_task_runner_thread_mode.h"
#include "base/threading/platform_thread.h"
#include "build/build_config.h"

namespace base {

class TaskTraits;
class SingleThreadTaskRunner;

namespace internal {

class DelayedTaskManager;
class SchedulerWorker;
class TaskTracker;

namespace {

class SchedulerWorkerDelegate;

}  // namespace

// Manages a pool of threads which are each associated with one or more
// SingleThreadTaskRunners.
//
// SingleThreadTaskRunners using SingleThreadTaskRunnerThreadMode::SHARED are
// backed by shared SchedulerWorkers for each COM+task environment combination.
// These workers are lazily instantiated and then only reclaimed during
// JoinForTesting()
//
// No threads are created (and hence no tasks can run) before Start() is called.
//
// This class is thread-safe.
class BASE_EXPORT SchedulerSingleThreadTaskRunnerManager final {
 public:
  SchedulerSingleThreadTaskRunnerManager(
      TaskTracker* task_tracker,
      DelayedTaskManager* delayed_task_manager);
  ~SchedulerSingleThreadTaskRunnerManager();

  // Starts threads for existing SingleThreadTaskRunners and allows threads to
  // be started when SingleThreadTaskRunners are created in the future.
  void Start();

  // Creates a SingleThreadTaskRunner which runs tasks with |traits| on a thread
  // named "TaskSchedulerSingleThread[Shared]" + |name| +
  // kEnvironmentParams[GetEnvironmentIndexForTraits(traits)].name_suffix +
  // index.
  scoped_refptr<SingleThreadTaskRunner> CreateSingleThreadTaskRunnerWithTraits(
      const std::string& name,
      const TaskTraits& traits,
      SingleThreadTaskRunnerThreadMode thread_mode);

#if defined(OS_WIN)
  // Creates a SingleThreadTaskRunner which runs tasks with |traits| on a COM
  // STA thread named "TaskSchedulerSingleThreadCOMSTA[Shared]" + |name| +
  // kEnvironmentParams[GetEnvironmentIndexForTraits(traits)].name_suffix +
  // index.
  scoped_refptr<SingleThreadTaskRunner> CreateCOMSTATaskRunnerWithTraits(
      const std::string& name,
      const TaskTraits& traits,
      SingleThreadTaskRunnerThreadMode thread_mode);
#endif  // defined(OS_WIN)

  void JoinForTesting();

 private:
  class SchedulerSingleThreadTaskRunner;

  template <typename DelegateType>
  scoped_refptr<SchedulerSingleThreadTaskRunner> CreateTaskRunnerWithTraitsImpl(
      const std::string& name,
      const TaskTraits& traits,
      SingleThreadTaskRunnerThreadMode thread_mode);

  template <typename DelegateType>
  std::unique_ptr<SchedulerWorkerDelegate> CreateSchedulerWorkerDelegate(
      const std::string& name,
      int id);

  template <typename DelegateType>
  SchedulerWorker* CreateAndRegisterSchedulerWorker(
      const std::string& name,
      ThreadPriority priority_hint);

  template <typename DelegateType>
  SchedulerWorker*& GetSharedSchedulerWorkerForTraits(const TaskTraits& traits);

  void UnregisterSchedulerWorker(SchedulerWorker* worker);

  void ReleaseSharedSchedulerWorkers();

  TaskTracker* const task_tracker_;
  DelayedTaskManager* const delayed_task_manager_;

  // Synchronizes access to all members below.
  SchedulerLock lock_;
  std::vector<scoped_refptr<SchedulerWorker>> workers_;
  int next_worker_id_ = 0;

  SchedulerWorker* shared_scheduler_workers_[ENVIRONMENT_COUNT] = {};

#if defined(OS_WIN)
  SchedulerWorker* shared_com_scheduler_workers_[ENVIRONMENT_COUNT] = {};
#endif  // defined(OS_WIN)

  // Set to true when Start() is called.
  bool started_ = false;

  DISALLOW_COPY_AND_ASSIGN(SchedulerSingleThreadTaskRunnerManager);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_SCHEDULER_SCHEDULER_SINGLE_THREAD_TASK_RUNNER_MANAGER_H_
