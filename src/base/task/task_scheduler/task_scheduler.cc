// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/task_scheduler.h"

#include <algorithm>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/system/sys_info.h"
#include "base/task/task_scheduler/scheduler_worker_pool_params.h"
#include "base/task/task_scheduler/task_scheduler_impl.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"

namespace base {

namespace {

// |g_task_scheduler| is intentionally leaked on shutdown.
TaskScheduler* g_task_scheduler = nullptr;

}  // namespace

TaskScheduler::InitParams::InitParams(
    const SchedulerWorkerPoolParams& background_worker_pool_params_in,
    const SchedulerWorkerPoolParams& foreground_worker_pool_params_in,
    SharedWorkerPoolEnvironment shared_worker_pool_environment_in)
    : background_worker_pool_params(background_worker_pool_params_in),
      foreground_worker_pool_params(foreground_worker_pool_params_in),
      shared_worker_pool_environment(shared_worker_pool_environment_in) {}

TaskScheduler::InitParams::~InitParams() = default;

TaskScheduler::ScopedExecutionFence::ScopedExecutionFence() {
  DCHECK(g_task_scheduler);
  g_task_scheduler->SetExecutionFenceEnabled(true);
}

TaskScheduler::ScopedExecutionFence::~ScopedExecutionFence() {
  DCHECK(g_task_scheduler);
  g_task_scheduler->SetExecutionFenceEnabled(false);
}

#if !defined(OS_NACL)
// static
void TaskScheduler::CreateAndStartWithDefaultParams(StringPiece name) {
  Create(name);
  GetInstance()->StartWithDefaultParams();
}

void TaskScheduler::StartWithDefaultParams() {
  // Values were chosen so that:
  // * There are few background threads.
  // * Background threads never outnumber foreground threads.
  // * The system is utilized maximally by foreground threads.
  // * The main thread is assumed to be busy, cap foreground workers at
  //   |num_cores - 1|.
  const int num_cores = SysInfo::NumberOfProcessors();

  // TODO(etiennep): Change this to 2.
  constexpr int kBackgroundMaxThreads = 3;
  const int kForegroundMaxThreads = std::max(3, num_cores - 1);

  constexpr TimeDelta kSuggestedReclaimTime = TimeDelta::FromSeconds(30);

  Start({{kBackgroundMaxThreads, kSuggestedReclaimTime},
         {kForegroundMaxThreads, kSuggestedReclaimTime}});
}
#endif  // !defined(OS_NACL)

void TaskScheduler::Create(StringPiece name) {
  SetInstance(std::make_unique<internal::TaskSchedulerImpl>(name));
}

// static
void TaskScheduler::SetInstance(std::unique_ptr<TaskScheduler> task_scheduler) {
  delete g_task_scheduler;
  g_task_scheduler = task_scheduler.release();
}

// static
TaskScheduler* TaskScheduler::GetInstance() {
  return g_task_scheduler;
}

}  // namespace base
