/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/base/build_config.h"

#include "perfetto/ext/base/platform.h"
#include "perfetto/ext/base/unix_task_runner.h"

#include <errno.h>
#include <stdlib.h>

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <Windows.h>
#include <synchapi.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <limits>

#include "perfetto/ext/base/watchdog.h"

namespace perfetto {
namespace base {

UnixTaskRunner::UnixTaskRunner() {
  AddFileDescriptorWatch(event_.fd(), [] {
    // Not reached -- see PostFileDescriptorWatches().
    PERFETTO_DFATAL("Should be unreachable.");
  });
}

UnixTaskRunner::~UnixTaskRunner() = default;

void UnixTaskRunner::WakeUp() {
  event_.Notify();
}

void UnixTaskRunner::Run() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  created_thread_id_.store(GetThreadId(), std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(lock_);
    quit_ = false;
  }
  for (;;) {
    int poll_timeout_ms;
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (quit_)
        return;
      poll_timeout_ms = GetDelayMsToNextTaskLocked();
      UpdateWatchTasksLocked();
    }

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    DWORD timeout =
        poll_timeout_ms >= 0 ? static_cast<DWORD>(poll_timeout_ms) : INFINITE;
    DWORD ret =
        WaitForMultipleObjects(static_cast<DWORD>(poll_fds_.size()),
                               &poll_fds_[0], /*bWaitAll=*/false, timeout);
    // Unlike poll(2), WaitForMultipleObjects() returns only *one* handle in the
    // set, even when >1 is signalled. In order to avoid starvation,
    // PostFileDescriptorWatches() will WaitForSingleObject() each other handle
    // to ensure fairness. |ret| here is passed just to avoid an extra
    // WaitForSingleObject() for the one handle that WaitForMultipleObject()
    // returned.
    PostFileDescriptorWatches(ret);
#else
    platform::BeforeMaybeBlockingSyscall();
    int ret = PERFETTO_EINTR(poll(
        &poll_fds_[0], static_cast<nfds_t>(poll_fds_.size()), poll_timeout_ms));
    platform::AfterMaybeBlockingSyscall();
    PERFETTO_CHECK(ret >= 0);
    PostFileDescriptorWatches(0 /*ignored*/);
#endif

    // To avoid starvation we always interleave all types of tasks -- immediate,
    // delayed and file descriptor watches.
    RunImmediateAndDelayedTask();
  }
}

void UnixTaskRunner::Quit() {
  std::lock_guard<std::mutex> lock(lock_);
  quit_ = true;
  WakeUp();
}

bool UnixTaskRunner::QuitCalled() {
  std::lock_guard<std::mutex> lock(lock_);
  return quit_;
}

bool UnixTaskRunner::IsIdleForTesting() {
  std::lock_guard<std::mutex> lock(lock_);
  return immediate_tasks_.empty();
}

void UnixTaskRunner::AdvanceTimeForTesting(uint32_t ms) {
  std::lock_guard<std::mutex> lock(lock_);
  advanced_time_for_testing_ += TimeMillis(ms);
}

void UnixTaskRunner::UpdateWatchTasksLocked() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  if (!watch_tasks_changed_)
    return;
  watch_tasks_changed_ = false;
#endif
  poll_fds_.clear();
  for (auto& it : watch_tasks_) {
    PlatformHandle handle = it.first;
    WatchTask& watch_task = it.second;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    if (!watch_task.pending)
      poll_fds_.push_back(handle);
#else
    watch_task.poll_fd_index = poll_fds_.size();
    poll_fds_.push_back({handle, POLLIN | POLLHUP, 0});
#endif
  }
}

void UnixTaskRunner::RunImmediateAndDelayedTask() {
  // If locking overhead becomes an issue, add a separate work queue.
  std::function<void()> immediate_task;
  std::function<void()> delayed_task;
  TimeMillis now = GetWallTimeMs();
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!immediate_tasks_.empty()) {
      immediate_task = std::move(immediate_tasks_.front());
      immediate_tasks_.pop_front();
    }
    if (!delayed_tasks_.empty()) {
      auto it = delayed_tasks_.begin();
      if (now + advanced_time_for_testing_ >= it->first) {
        delayed_task = std::move(it->second);
        delayed_tasks_.erase(it);
      }
    }
  }

  errno = 0;
  if (immediate_task)
    RunTaskWithWatchdogGuard(immediate_task);
  errno = 0;
  if (delayed_task)
    RunTaskWithWatchdogGuard(delayed_task);
}

void UnixTaskRunner::PostFileDescriptorWatches(uint64_t windows_wait_result) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (size_t i = 0; i < poll_fds_.size(); i++) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    const PlatformHandle handle = poll_fds_[i];
    // |windows_wait_result| is the result of WaitForMultipleObjects() call. If
    // one of the objects was signalled, it will have a value between
    // [0, poll_fds_.size()].
    if (i != windows_wait_result &&
        WaitForSingleObject(handle, 0) != WAIT_OBJECT_0) {
      continue;
    }
#else
    base::ignore_result(windows_wait_result);
    const PlatformHandle handle = poll_fds_[i].fd;
    if (!(poll_fds_[i].revents & (POLLIN | POLLHUP)))
      continue;
    poll_fds_[i].revents = 0;
#endif

    // The wake-up event is handled inline to avoid an infinite recursion of
    // posted tasks.
    if (handle == event_.fd()) {
      event_.Clear();
      continue;
    }

    // Binding to |this| is safe since we are the only object executing the
    // task.
    PostTask(std::bind(&UnixTaskRunner::RunFileDescriptorWatch, this, handle));

    // Flag the task as pending.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    // On Windows this is done by marking the WatchTask entry as pending. This
    // is more expensive than Linux as requires rebuilding the |poll_fds_|
    // vector on each call. There doesn't seem to be a good alternative though.
    auto it = watch_tasks_.find(handle);
    PERFETTO_CHECK(it != watch_tasks_.end());
    PERFETTO_DCHECK(!it->second.pending);
    it->second.pending = true;
#else
    // On UNIX systems instead, we just make the fd negative while its task is
    // pending. This makes poll(2) ignore the fd.
    PERFETTO_DCHECK(poll_fds_[i].fd >= 0);
    poll_fds_[i].fd = -poll_fds_[i].fd;
#endif
  }
}

void UnixTaskRunner::RunFileDescriptorWatch(PlatformHandle fd) {
  std::function<void()> task;
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto it = watch_tasks_.find(fd);
    if (it == watch_tasks_.end())
      return;
    WatchTask& watch_task = it->second;

    // Make poll(2) pay attention to the fd again. Since another thread may have
    // updated this watch we need to refresh the set first.
    UpdateWatchTasksLocked();

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    // On Windows we manually track the presence of outstanding tasks for the
    // watch. The UpdateWatchTasksLocked() in the Run() loop will re-add the
    // task to the |poll_fds_| vector.
    PERFETTO_DCHECK(watch_task.pending);
    watch_task.pending = false;
#else
    size_t fd_index = watch_task.poll_fd_index;
    PERFETTO_DCHECK(fd_index < poll_fds_.size());
    PERFETTO_DCHECK(::abs(poll_fds_[fd_index].fd) == fd);
    poll_fds_[fd_index].fd = fd;
#endif
    task = watch_task.callback;
  }
  errno = 0;
  RunTaskWithWatchdogGuard(task);
}

int UnixTaskRunner::GetDelayMsToNextTaskLocked() const {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!immediate_tasks_.empty())
    return 0;
  if (!delayed_tasks_.empty()) {
    TimeMillis diff = delayed_tasks_.begin()->first - GetWallTimeMs() -
                      advanced_time_for_testing_;
    return std::max(0, static_cast<int>(diff.count()));
  }
  return -1;
}

void UnixTaskRunner::PostTask(std::function<void()> task) {
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock(lock_);
    was_empty = immediate_tasks_.empty();
    immediate_tasks_.push_back(std::move(task));
  }
  if (was_empty)
    WakeUp();
}

void UnixTaskRunner::PostDelayedTask(std::function<void()> task,
                                     uint32_t delay_ms) {
  TimeMillis runtime = GetWallTimeMs() + TimeMillis(delay_ms);
  {
    std::lock_guard<std::mutex> lock(lock_);
    delayed_tasks_.insert(
        std::make_pair(runtime + advanced_time_for_testing_, std::move(task)));
  }
  WakeUp();
}

void UnixTaskRunner::AddFileDescriptorWatch(PlatformHandle fd,
                                            std::function<void()> task) {
  PERFETTO_DCHECK(PlatformHandleChecker::IsValid(fd));
  {
    std::lock_guard<std::mutex> lock(lock_);
    PERFETTO_DCHECK(!watch_tasks_.count(fd));
    WatchTask& watch_task = watch_tasks_[fd];
    watch_task.callback = std::move(task);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    watch_task.pending = false;
#else
    watch_task.poll_fd_index = SIZE_MAX;
#endif
    watch_tasks_changed_ = true;
  }
  WakeUp();
}

void UnixTaskRunner::RemoveFileDescriptorWatch(PlatformHandle fd) {
  PERFETTO_DCHECK(PlatformHandleChecker::IsValid(fd));
  {
    std::lock_guard<std::mutex> lock(lock_);
    PERFETTO_DCHECK(watch_tasks_.count(fd));
    watch_tasks_.erase(fd);
    watch_tasks_changed_ = true;
  }
  // No need to schedule a wake-up for this.
}

bool UnixTaskRunner::RunsTasksOnCurrentThread() const {
  return GetThreadId() == created_thread_id_.load(std::memory_order_relaxed);
}

}  // namespace base
}  // namespace perfetto
