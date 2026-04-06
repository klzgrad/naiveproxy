/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_EXT_BASE_LOCK_FREE_TASK_RUNNER_H_
#define INCLUDE_PERFETTO_EXT_BASE_LOCK_FREE_TASK_RUNNER_H_

#include "perfetto/base/flat_set.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/thread_annotations.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/event_fd.h"
#include "perfetto/ext/base/flags.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/unix_task_runner.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <poll.h>
#endif

#include <array>
#include <atomic>
#include <thread>
#include <unordered_map>

namespace perfetto {
namespace base {

namespace task_runner_internal {
class ScopedRefcount;
struct Slab;

// Exposed for testing
constexpr uint32_t kNumRefcountBuckets = 32;
constexpr size_t kSlabSize = 512;

}  // namespace task_runner_internal

template <typename T>
class TaskRunnerTest;

// This class implements a lock-less multi-producer single-consumer task runner.
// This is achieved by using a linked list of "slabs". Each slab is a fixed-size
// array of tasks.
// See /docs/design-docs/lock-free-task-runner.md for more details
class PERFETTO_EXPORT_COMPONENT LockFreeTaskRunner : public TaskRunner {
 public:
  LockFreeTaskRunner();
  ~LockFreeTaskRunner() override;

  void Run();
  void Quit();

  // Checks whether there are any pending immediate tasks to run. Note that
  // delayed tasks don't count even if they are due to run.
  bool IsIdleForTesting();

  // TaskRunner implementation:
  void PostTask(std::function<void()>) override;
  void PostDelayedTask(std::function<void()>, uint32_t delay_ms) override;
  void AddFileDescriptorWatch(PlatformHandle, std::function<void()>) override;
  void RemoveFileDescriptorWatch(PlatformHandle) override;
  bool RunsTasksOnCurrentThread() const override;

  // Pretends (for the purposes of running delayed tasks) that time advanced by
  // `ms`.
  void AdvanceTimeForTesting(uint32_t ms);

  // Stats for testing.
  size_t slabs_allocated() const {
    return slabs_allocated_.load(std::memory_order_relaxed);
  }
  size_t slabs_freed() const {
    return slabs_freed_.load(std::memory_order_relaxed);
  }

 private:
  using Slab = task_runner_internal::Slab;
  using ScopedRefcount = task_runner_internal::ScopedRefcount;
  friend class task_runner_internal::ScopedRefcount;

  struct DelayedTask {
    TimeMillis time;
    uint64_t seq;
    std::function<void()> task;

    // Note that the < operator keeps the DelayedTasks sorted in reverse order
    // (the latest one is first, the earliest one is last). This is so we can
    // have a FIFO queue using a vector by just doing an O(1) pop_back().
    bool operator<(const DelayedTask& other) const {
      if (time != other.time)
        return time > other.time;
      return seq > other.seq;
    }
    bool operator==(const DelayedTask& other) const {
      return time == other.time && seq == other.seq;
    }
  };

  PERFETTO_ALWAYS_INLINE std::function<void()> PopNextImmediateTask();
  std::function<void()> PopTaskRecursive(Slab*, Slab* next_slab);
  std::function<void()> PopNextExpiredDelayedTask();
  int GetDelayMsToNextTask() const;
  void WakeUp() { wakeup_event_.Notify(); }
  Slab* AllocNewSlab();
  void DeleteSlab(Slab*);
  void PostFileDescriptorWatches(uint64_t windows_wait_result);
  void RunFileDescriptorWatch(PlatformHandle);
  void UpdateWatchTasks();

  // These two are semantically a unique_ptr, but are accessed from different
  // threads.
  std::atomic<Slab*> tail_{};  // This is never null.
  std::atomic<Slab*> free_slab_{};

  EventFd wakeup_event_;
  bool quit_ = false;
  std::thread::id run_task_thread_id_;

  // Delayed tasks, accessed only by the main thread. Items are stored in
  // reverse temporal order, see comment in the operator<.
  FlatSet<DelayedTask> delayed_tasks_;
  uint64_t next_delayed_task_seq_ = 0;
  std::atomic<uint32_t> advanced_time_for_testing_{};

  // The array of fds/handles passed to poll(2) / WaitForMultipleObjects().
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  std::vector<PlatformHandle> poll_fds_;
#else
  std::vector<struct pollfd> poll_fds_;
#endif

  struct WatchTask {
    std::function<void()> callback;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    // On UNIX systems we make the FD number negative in |poll_fds_| to avoid
    // polling it again until the queued task runs. On Windows we can't do that.
    // Instead we keep track of its state here.
    bool pending = false;
#else
    size_t poll_fd_index;  // Index into |poll_fds_|.
#endif
  };

  // Accessed only from the main thread.
  std::unordered_map<PlatformHandle, WatchTask> watch_tasks_;
  bool watch_tasks_changed_ = false;

  // An array of 32 refcount buckets. Every Slab* maps to a bucket via a hash
  // function. Every PostTask() thread increases the refcount before accessing
  // a slab, and decreases it when done.
  // This allows the Run() main thread to tell if any thread has possibly been
  // able to observe the Slab through the tail_ before deleting it.
  std::array<std::atomic<int32_t>, task_runner_internal::kNumRefcountBuckets>
      refcounts_{};

  // Stats for testing.
  std::atomic<size_t> slabs_allocated_{};
  std::atomic<size_t> slabs_freed_{};
};

namespace task_runner_internal {

// Returns the index of the refcount_ bucket for the passed Slab pointer.
static uint32_t HashSlabPtr(Slab* slab) {
  // This is a SplitMix64 hash, which is very fast and effective with pointers
  // (See the test LockFreeTaskRunnerTest.HashSpreading).
  uint64_t u = reinterpret_cast<uintptr_t>(slab);
  u &= 0x00FFFFFFFFFFFFFFull;  // Clear asan/MTE top byte for tagged pointers.
  u += 0x9E3779B97F4A7C15ull;
  u = (u ^ (u >> 30)) * 0xBF58476D1CE4E5B9ull;
  u = (u ^ (u >> 27)) * 0x94D049BB133111EBull;
  return static_cast<uint32_t>((u ^ (u >> 31)) % kNumRefcountBuckets);
}

// A slab is a fixed-size array of tasks. The lifecycle of a task slot
// within a slab goes through three phases:
//
// 1. Reservation: A writer thread atomically increments `next_task_slot` to
//    reserve a slot in the `tasks` array. This reservation establishes the
//    implicit order in which the consumer will attempt to read tasks (but
//    only if they are published in the bitmap, see below).
//
// 2. Publishing: After writing the task into its reserved slot, the writer
//    thread atomically sets the corresponding bit in the `tasks_written`
//    bitmask. This acts as a memory barrier and makes the task visible to
//    the consumer (main) thread.
//
// 3. Consumption: The main thread acquire-reads the `tasks_written` bitmask.
//    For each bit that is set, it processes the task and then sets the
//    corresponding bit in its private `tasks_read` bitmask to prevent
//    reading the same task again.
struct Slab {
  Slab();
  ~Slab();

  std::atomic<size_t> next_task_slot{0};

  // `tasks` and `next_task_slot` are accessed by writer threads only.
  // The main thread can access `tasks[i]` but only after ensuring that the
  // corresponding bit in `tasks_written` is set.
  std::array<std::function<void()>, kSlabSize> tasks{};

  // A bitmask indicating which tasks in the `tasks` array have been written
  // and are ready to be read by the main thread.
  // This is atomically updated by writer threads and read by the main thread.
  using BitWord = size_t;
  static constexpr size_t kBitsPerWord = sizeof(BitWord) * 8;
  static constexpr size_t kNumWords = kSlabSize / kBitsPerWord;
  std::array<std::atomic<BitWord>, kNumWords> tasks_written{};

  // A bitmask indicating which tasks have been read by the main thread.
  // This is accessed only by the main thread, so no atomicity is required.
  std::array<BitWord, kNumWords> tasks_read{};

  // The link to the previous slab.
  // This is written by writer threads when they create a new slab and link it
  // to the previous tail. But they do so when nobody else can see the Slab,
  // so there is no need for an atomic ptr. After the initial creation,
  // this is accessed only by the main thread when:
  // 1. draining tasks (to walk back to the oldest slab)
  // 2. deleting slabs, setting it to nullptr, when they are fully consumed.
  Slab* prev = nullptr;
};

class ScopedRefcount {
 public:
  ScopedRefcount(LockFreeTaskRunner* tr, LockFreeTaskRunner::Slab* slab) {
    bucket_ = &tr->refcounts_[HashSlabPtr(slab)];
    auto prev_value = bucket_->fetch_add(1);
    PERFETTO_DCHECK(prev_value >= 0);
  }

  ~ScopedRefcount() {
    if (bucket_) {
      auto prev_value = bucket_->fetch_sub(1);
      PERFETTO_DCHECK(prev_value > 0);
    }
  }

  ScopedRefcount(ScopedRefcount&& other) noexcept {
    bucket_ = other.bucket_;
    other.bucket_ = nullptr;
  }

  ScopedRefcount& operator=(ScopedRefcount&& other) noexcept {
    this->~ScopedRefcount();
    new (this) ScopedRefcount(std::move(other));
    return *this;
  }

  ScopedRefcount(const ScopedRefcount&) = delete;
  ScopedRefcount& operator=(const ScopedRefcount&) = delete;

  std::atomic<int32_t>* bucket_{};
};
}  // namespace task_runner_internal

using MaybeLockFreeTaskRunner =
    std::conditional_t<PERFETTO_FLAGS(USE_LOCKFREE_TASKRUNNER),
                       LockFreeTaskRunner,
                       UnixTaskRunner>;

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_LOCK_FREE_TASK_RUNNER_H_
