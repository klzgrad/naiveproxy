// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/environment_config.h"

#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "build/build_config.h"

namespace base {
namespace internal {

size_t GetEnvironmentIndexForTraits(const TaskTraits& traits) {
  const bool is_background =
      traits.priority() == base::TaskPriority::BEST_EFFORT;
  if (traits.may_block() || traits.with_base_sync_primitives())
    return is_background ? BACKGROUND_BLOCKING : FOREGROUND_BLOCKING;
  return is_background ? BACKGROUND : FOREGROUND;
}

bool CanUseBackgroundPriorityForSchedulerWorker() {
  // When Lock doesn't handle multiple thread priorities, run all
  // SchedulerWorker with a normal priority to avoid priority inversion when a
  // thread running with a normal priority tries to acquire a lock held by a
  // thread running with a background priority.
  if (!Lock::HandlesMultipleThreadPriorities())
    return false;

#if !defined(OS_ANDROID)
  // When thread priority can't be increased, run all threads with a normal
  // priority to avoid priority inversions on shutdown (TaskScheduler increases
  // background threads priority to normal on shutdown while resolving remaining
  // shutdown blocking tasks).
  //
  // This is ignored on Android, because it doesn't have a clean shutdown phase.
  if (!PlatformThread::CanIncreaseCurrentThreadPriority())
    return false;
#endif  // defined(OS_ANDROID)

  return true;
}

}  // namespace internal
}  // namespace base
