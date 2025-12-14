/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "perfetto/ext/base/thread_task_runner.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/lock_free_task_runner.h"
#include "perfetto/ext/base/thread_utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/prctl.h>
#endif

namespace perfetto {
namespace base {

ThreadTaskRunner::ThreadTaskRunner(ThreadTaskRunner&& other) noexcept
    : thread_(std::move(other.thread_)), task_runner_(other.task_runner_) {
  other.task_runner_ = nullptr;
}

ThreadTaskRunner& ThreadTaskRunner::operator=(ThreadTaskRunner&& other) {
  this->~ThreadTaskRunner();
  new (this) ThreadTaskRunner(std::move(other));
  return *this;
}

ThreadTaskRunner::~ThreadTaskRunner() {
  if (task_runner_) {
    task_runner_->Quit();

    PERFETTO_DCHECK(thread_.joinable());
  }
  if (thread_.joinable())
    thread_.join();
}

ThreadTaskRunner::ThreadTaskRunner(const std::string& name) : name_(name) {
  std::mutex init_lock;
  std::condition_variable init_cv;

  std::function<void(MaybeLockFreeTaskRunner*)> initializer =
      [this, &init_lock, &init_cv](MaybeLockFreeTaskRunner* task_runner) {
        std::lock_guard<std::mutex> lock(init_lock);
        task_runner_ = task_runner;
        // Notify while still holding the lock, as init_cv ceases to exist as
        // soon as the main thread observes a non-null task_runner_, and it can
        // wake up spuriously (i.e. before the notify if we had unlocked before
        // notifying).
        init_cv.notify_one();
      };

  thread_ = std::thread(&ThreadTaskRunner::RunTaskThread, this,
                        std::move(initializer));

  std::unique_lock<std::mutex> lock(init_lock);
  init_cv.wait(lock, [this] { return !!task_runner_; });
}

void ThreadTaskRunner::RunTaskThread(
    std::function<void(MaybeLockFreeTaskRunner*)> initializer) {
  if (!name_.empty()) {
    base::MaybeSetThreadName(name_);
  }

  MaybeLockFreeTaskRunner task_runner;
  task_runner.PostTask(std::bind(std::move(initializer), &task_runner));
  task_runner.Run();
}

void ThreadTaskRunner::PostTaskAndWaitForTesting(std::function<void()> fn) {
  std::mutex mutex;
  std::condition_variable cv;

  std::unique_lock<std::mutex> lock(mutex);
  bool done = false;
  task_runner_->PostTask([&mutex, &cv, &done, &fn] {
    fn();

    std::lock_guard<std::mutex> inner_lock(mutex);
    done = true;
    cv.notify_one();
  });
  cv.wait(lock, [&done] { return done; });
}

uint64_t ThreadTaskRunner::GetThreadCPUTimeNsForTesting() {
  uint64_t thread_time_ns = 0;
  PostTaskAndWaitForTesting([&thread_time_ns] {
    thread_time_ns = static_cast<uint64_t>(base::GetThreadCPUTimeNs().count());
  });
  return thread_time_ns;
}
PlatformThreadId ThreadTaskRunner::GetThreadIdForTesting() {
  PlatformThreadId thread_id = static_cast<PlatformThreadId>(-1);
  PostTaskAndWaitForTesting([&thread_id] { thread_id = GetThreadId(); });
  return thread_id;
}

void ThreadTaskRunner::PostTask(std::function<void()> task) {
  task_runner_->PostTask(std::move(task));
}

void ThreadTaskRunner::PostDelayedTask(std::function<void()> task,
                                       uint32_t delay_ms) {
  task_runner_->PostDelayedTask(std::move(task), delay_ms);
}

void ThreadTaskRunner::AddFileDescriptorWatch(
    PlatformHandle handle,
    std::function<void()> watch_task) {
  task_runner_->AddFileDescriptorWatch(handle, std::move(watch_task));
}

void ThreadTaskRunner::RemoveFileDescriptorWatch(PlatformHandle handle) {
  task_runner_->RemoveFileDescriptorWatch(handle);
}

bool ThreadTaskRunner::RunsTasksOnCurrentThread() const {
  return task_runner_->RunsTasksOnCurrentThread();
}

}  // namespace base
}  // namespace perfetto
