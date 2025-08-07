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

#include "perfetto/ext/base/threading/spawn.h"

#include <optional>

#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/thread_checker.h"
#include "perfetto/ext/base/threading/future.h"
#include "perfetto/ext/base/threading/poll.h"
#include "perfetto/ext/base/threading/stream.h"

namespace perfetto {
namespace base {

// Represents a future which is being polled to completion. Owned by
// SpawnHandle.
class PolledFuture {
 public:
  explicit PolledFuture(TaskRunner* task_runner, Future<FVoid> future)
      : task_runner_(task_runner), future_(std::move(future)) {
    PERFETTO_DCHECK(task_runner_->RunsTasksOnCurrentThread());
    PollUntilFinish();
  }

  ~PolledFuture() {
    PERFETTO_DCHECK_THREAD(thread_checker);
    ClearFutureAndWatches(interested_);
  }

 private:
  PolledFuture(PolledFuture&&) = delete;
  PolledFuture& operator=(PolledFuture&&) = delete;

  void PollUntilFinish() {
    PERFETTO_DCHECK(task_runner_->RunsTasksOnCurrentThread());

    auto pre_poll_interested = std::move(interested_);
    interested_.clear();

    FuturePollResult<FVoid> res = future_->Poll(&context_);
    if (!res.IsPending()) {
      ClearFutureAndWatches(pre_poll_interested);
      return;
    }

    for (PlatformHandle fd : SetDifference(pre_poll_interested, interested_)) {
      task_runner_->RemoveFileDescriptorWatch(fd);
    }

    auto weak_this = weak_ptr_factory_.GetWeakPtr();
    for (PlatformHandle fd : SetDifference(interested_, pre_poll_interested)) {
      task_runner_->AddFileDescriptorWatch(fd, [weak_this, fd]() {
        if (!weak_this) {
          return;
        }
        weak_this->ready_ = {fd};
        weak_this->PollUntilFinish();
      });
    }
  }

  void ClearFutureAndWatches(const FlatSet<PlatformHandle>& interested) {
    future_ = std::nullopt;
    for (PlatformHandle fd : interested) {
      task_runner_->RemoveFileDescriptorWatch(fd);
    }
    interested_.clear();
    ready_.clear();
  }

  static std::vector<PlatformHandle> SetDifference(
      const FlatSet<PlatformHandle>& f,
      const FlatSet<PlatformHandle>& s) {
    std::vector<PlatformHandle> out(f.size());
    auto it = std::set_difference(f.begin(), f.end(), s.begin(), s.end(),
                                  out.begin());
    out.resize(static_cast<size_t>(std::distance(out.begin(), it)));
    return out;
  }

  TaskRunner* const task_runner_ = nullptr;

  std::optional<Future<FVoid>> future_;
  FlatSet<PlatformHandle> interested_;
  FlatSet<PlatformHandle> ready_;
  PollContext context_{&interested_, &ready_};

  PERFETTO_THREAD_CHECKER(thread_checker)

  // Keep this last.
  WeakPtrFactory<PolledFuture> weak_ptr_factory_{this};
};

SpawnHandle::SpawnHandle(TaskRunner* task_runner,
                         std::function<Future<FVoid>()> fn)
    : task_runner_(task_runner),
      polled_future_(std::make_shared<std::unique_ptr<PolledFuture>>()) {
  task_runner->PostTask(
      [t = task_runner, fn = std::move(fn), p = polled_future_]() mutable {
        p->reset(new PolledFuture(t, fn()));
      });
}

SpawnHandle::~SpawnHandle() {
  task_runner_->PostTask(
      [f = std::move(polled_future_)]() mutable { f.reset(); });
}

}  // namespace base
}  // namespace perfetto
