/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "perfetto/ext/base/threading/thread_pool.h"
#include <mutex>
#include <thread>

namespace perfetto {
namespace base {

ThreadPool::ThreadPool(uint32_t thread_count) {
  for (uint32_t i = 0; i < thread_count; ++i) {
    threads_.emplace_back(std::bind(&ThreadPool::RunThreadLoop, this));
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    quit_ = true;
  }
  thread_waiter_.notify_all();
  for (auto& thread : threads_) {
    thread.join();
  }
}

void ThreadPool::PostTask(std::function<void()> fn) {
  std::lock_guard<std::mutex> guard(mutex_);
  pending_tasks_.emplace_back(std::move(fn));
  if (thread_waiting_count_ == 0) {
    return;
  }
  thread_waiter_.notify_one();
}

void ThreadPool::RunThreadLoop() PERFETTO_NO_THREAD_SAFETY_ANALYSIS {
  // 'std::unique_lock' lock doesn't work well with thread annotations
  // (see https://github.com/llvm/llvm-project/issues/63239),
  // so we suppress thread safety static analysis for this method.
  for (;;) {
    std::function<void()> fn;
    {
      std::unique_lock<std::mutex> guard(mutex_);
      if (quit_) {
        return;
      }
      if (pending_tasks_.empty()) {
        thread_waiting_count_++;
        thread_waiter_.wait(guard,
                            [this]() PERFETTO_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
                              return quit_ || !pending_tasks_.empty();
                            });
        thread_waiting_count_--;
        continue;
      }
      fn = std::move(pending_tasks_.front());
      pending_tasks_.pop_front();
    }
    fn();
  }
}

}  // namespace base
}  // namespace perfetto
