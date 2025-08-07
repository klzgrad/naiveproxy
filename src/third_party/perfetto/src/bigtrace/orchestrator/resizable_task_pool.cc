/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/bigtrace/orchestrator/resizable_task_pool.h"

namespace perfetto::bigtrace {

ResizableTaskPool::ResizableTaskPool(std::function<void(ThreadWithContext*)> fn)
    : fn_(std::move(fn)) {}

// Resizes the number of threads in the task pool to |new_size|
//
// This works by performing one of two possible actions:
// 1) When the number of threads is reduced, the excess are cancelled and joined
// 2) When the number of threads is increased, new threads are created and
// started
void ResizableTaskPool::Resize(uint32_t new_size) {
  if (size_t old_size = contextual_threads_.size(); new_size < old_size) {
    for (size_t i = new_size; i < old_size; ++i) {
      contextual_threads_[i]->Cancel();
    }
    for (size_t i = new_size; i < old_size; ++i) {
      contextual_threads_[i]->thread.join();
    }
    contextual_threads_.resize(new_size);
  } else {
    contextual_threads_.resize(new_size);
    for (size_t i = old_size; i < new_size; ++i) {
      contextual_threads_[i] = std::make_unique<ThreadWithContext>(fn_);
    }
  }
}

// Joins all threads in the task pool
void ResizableTaskPool::JoinAll() {
  for (auto& contextual_thread : contextual_threads_) {
    contextual_thread->thread.join();
  }
}

}  // namespace perfetto::bigtrace
