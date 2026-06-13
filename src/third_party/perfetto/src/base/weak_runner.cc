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

#include "perfetto/ext/base/weak_runner.h"

#include "perfetto/base/task_runner.h"

namespace perfetto::base {

WeakRunner::WeakRunner(base::TaskRunner* task_runner)
    : task_runner_(task_runner), destroyed_(std::make_shared<bool>(false)) {}

WeakRunner::~WeakRunner() {
  *destroyed_ = true;
}

void WeakRunner::PostTask(std::function<void()> f) const {
  task_runner_->PostTask([destroyed = destroyed_, f = std::move(f)]() {
    if (*destroyed) {
      return;
    }
    f();
  });
}

void WeakRunner::PostDelayedTask(std::function<void()> f,
                                 uint32_t delay_ms) const {
  task_runner_->PostDelayedTask(
      [destroyed = destroyed_, f = std::move(f)]() {
        if (*destroyed) {
          return;
        }
        f();
      },
      delay_ms);
}

}  // namespace perfetto::base
