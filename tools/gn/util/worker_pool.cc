// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/worker_pool.h"

#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "tools/gn/switches.h"
#include "util/sys_info.h"

namespace {

int GetThreadCount() {
  std::string thread_count =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kThreads);

  // See if an override was specified on the command line.
  int result;
  if (!thread_count.empty() && base::StringToInt(thread_count, &result) &&
      result >= 1) {
    return result;
  }

  // Base the default number of worker threads on number of cores in the
  // system. When building large projects, the speed can be limited by how fast
  // the main thread can dispatch work and connect the dependency graph. If
  // there are too many worker threads, the main thread can be starved and it
  // will run slower overall.
  //
  // One less worker thread than the number of physical CPUs seems to be a
  // good value, both theoretically and experimentally. But always use at
  // least some workers to prevent us from being too sensitive to I/O latency
  // on low-end systems.
  //
  // The minimum thread count is based on measuring the optimal threads for the
  // Chrome build on a several-year-old 4-core MacBook.
  // Almost all CPUs now are hyperthreaded.
  int num_cores = NumberOfProcessors() / 2;
  return std::max(num_cores - 1, 8);
}

}  // namespace

WorkerPool::WorkerPool() : WorkerPool(GetThreadCount()) {}

WorkerPool::WorkerPool(size_t thread_count) : should_stop_processing_(false) {
  threads_.reserve(thread_count);
  for (size_t i = 0; i < thread_count; ++i)
    threads_.emplace_back([this]() { Worker(); });
}

WorkerPool::~WorkerPool() {
  {
    std::unique_lock<std::mutex> queue_lock(queue_mutex_);
    should_stop_processing_ = true;
  }

  pool_notifier_.notify_all();

  for (auto& task_thread : threads_) {
    task_thread.join();
  }
}

void WorkerPool::PostTask(Task work) {
  {
    std::unique_lock<std::mutex> queue_lock(queue_mutex_);
    CHECK(!should_stop_processing_);
    task_queue_.emplace(std::move(work));
  }

  pool_notifier_.notify_one();
}

void WorkerPool::Worker() {
  for (;;) {
    Task task;

    {
      std::unique_lock<std::mutex> queue_lock(queue_mutex_);

      pool_notifier_.wait(queue_lock, [this]() {
        return (!task_queue_.empty()) || should_stop_processing_;
      });

      if (should_stop_processing_ && task_queue_.empty())
        return;

      task = std::move(task_queue_.front());
      task_queue_.pop();
    }

    std::move(task).Run();
  }
}
