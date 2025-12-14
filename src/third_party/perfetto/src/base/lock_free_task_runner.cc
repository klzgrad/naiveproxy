
/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law of an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/base/lock_free_task_runner.h"

#include "perfetto/base/build_config.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <windows.h>

// Keep the \n before to prevent clang-format reordering.
#include <synchapi.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include <thread>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/bits.h"
#include "perfetto/ext/base/platform.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/watchdog.h"

namespace perfetto {
namespace base {

namespace task_runner_internal {
Slab::Slab() = default;
Slab::~Slab() {
  PERFETTO_DCHECK(!prev);  // We should never delete linked slabs.
}
}  // namespace task_runner_internal

namespace {
static constexpr auto kSlabSize = task_runner_internal::kSlabSize;
}

LockFreeTaskRunner::LockFreeTaskRunner()
    : run_task_thread_id_(std::this_thread::get_id()) {
  static_assert((kSlabSize & (kSlabSize - 1)) == 0, "kSlabSize must be a pow2");
  static_assert(kSlabSize >= Slab::kBitsPerWord);

  // Populate both the tail and the freelist, to minimize the chance of
  // allocations at runtime.
  tail_.store(AllocNewSlab());
  free_slab_.store(AllocNewSlab());

  AddFileDescriptorWatch(wakeup_event_.fd(), [] {
    // Not reached -- see PostFileDescriptorWatches().
    PERFETTO_DFATAL("Should be unreachable.");
  });
}

LockFreeTaskRunner::~LockFreeTaskRunner() {
  PERFETTO_DCHECK(RunsTasksOnCurrentThread());

  for (Slab* slab = tail_.exchange(nullptr); slab;) {
    Slab* prev = slab->prev;
    slab->prev = nullptr;
    delete slab;
    slab = prev;
  }
  delete free_slab_.exchange(nullptr);
}

void LockFreeTaskRunner::PostTask(std::function<void()> closure) {
  // We use nullity of std::function in PopTaskRecursive() to determine exit
  // criteria. Posting a null task would break that logic. Also a null task
  // would cause an abort when trying to run it later on.
  PERFETTO_CHECK(PERFETTO_LIKELY(closure));
  using BitWord = Slab::BitWord;
  for (;;) {
    Slab* slab = tail_.load();
    PERFETTO_DCHECK(slab);  // The tail_ must be always valid.
    ScopedRefcount scoped_refcount(this, slab);

    // Now that we have a slab, try appending a task to it (if there is space).
    // We have 3 cases:
    // 1. slot < kSlabSize: the nominal case. Append the task and return.
    // 2. slot == kSlabSize: the common overflow case: The slab was full and we
    //    tried to allocate the N+1 th element. We have to allocate a new Slab.
    // 3. slot > kSlabSize: like 2, but but two (or more) threads raced on it.
    //    One thread will win the race and alloc a new slab, the other one will
    //    repeat.
    size_t slot = slab->next_task_slot.fetch_add(1, std::memory_order_relaxed);

    if (slot >= kSlabSize) {  // Cases 2,3
      Slab* new_slab = AllocNewSlab();
      new_slab->prev = slab;
      new_slab->next_task_slot.store(1, std::memory_order_relaxed);
      slot = 0;
      if (PERFETTO_UNLIKELY(!tail_.compare_exchange_strong(slab, new_slab))) {
        // If the cmpxcgh fails, another thread tried to allocate a new tail
        // slab and won the race. Do another round, we'll observe the new slab.
        // We have to release the prev pointer as at this point we found out
        // another thread also has it.
        new_slab->prev = nullptr;
        DeleteSlab(new_slab);
        continue;
      }

      slab = new_slab;
      scoped_refcount = ScopedRefcount(this, new_slab);
    }

    // Nominal case: publish the task and return.
    PERFETTO_DCHECK(!slab->tasks[slot]);
    slab->tasks[slot] = std::move(closure);
    size_t s_word = slot / Slab::kBitsPerWord;
    size_t s_bit = slot % Slab::kBitsPerWord;
    BitWord s_mask = BitWord(1) << s_bit;
    PERFETTO_DCHECK(
        (slab->tasks_written[s_word].load(std::memory_order_relaxed) &
         s_mask) == 0);
    slab->tasks_written[s_word].fetch_or(s_mask, std::memory_order_release);

    if (!RunsTasksOnCurrentThread()) {
      // We don't need any clever logic to avoid spurious wake ups from other
      // threads. Most PostTask()s in our codebase are done by the main thread.
      // In the rare cases of a PostTask() coming from another thread, the odds
      // of the main thread being woken up at the same time are tiny.
      WakeUp();
    }
    return;
  }
}

void LockFreeTaskRunner::Run() {
  PERFETTO_CHECK(run_task_thread_id_ == std::this_thread::get_id());
  quit_ = false;

  while (!quit_) {
    // Pop one immediate and one delayed task, for fairness.
    std::function<void()> immediate_task = PopNextImmediateTask();
    std::function<void()> delayed_task = PopNextExpiredDelayedTask();
    bool has_task = immediate_task || delayed_task;
    int poll_timeout_ms = has_task ? 0 : GetDelayMsToNextTask();

    // run the poll(). We need it for two different reasons:
    // 1. Blocks until the next event on the horizon, which:
    //    - If we pulled a task, poll in non-blocking mode (0 delay). There
    //      might be other tasks ready and we want to run them soon after.
    //    - If there is a delayed task, compute the time remaining for it.
    //    - Otherwise polls indefinitely, waiting for a PostTask() or a Quit()
    //      call from another thread.
    // 2. Regardless of timing, we need to read the FD watches.
    //    We want to do this even if we know already that we have an immediate
    //    task (when poll_timeout_ms = 0) to ensure fairness.

    //    TODO(primiano): we could optimize this and avoid a syscall for each
    //    task when we have bursts of tasks (with some saturation, e.g. do still
    //    a poll() every 10 tasks). Unfortunately, however, some users of
    //    TestTaskRunner.RunUntilIdle() rely on the fact that FD watches are
    //    polled before the current task is ran, so that the task can tell if
    //    there are more tasks upcoming or it has reached quiescence.
    //    TL;DR RunUntilIdle() was a mistake as it has ill-defined semantics but
    //    now tests rely on those subtle semantics.

    // Recompute the list of FDs to watch.
    UpdateWatchTasks();

    uint64_t windows_wait_res = 0;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    // Unlike poll(2), WaitForMultipleObjects() returns only *one* handle in the
    // set, even when >1 is signalled. In order to avoid starvation,
    // PostFileDescriptorWatches() will WaitForSingleObject() each other handle
    // to ensure fairness. |windows_wait_res| is passed just to avoid an extra
    // WaitForSingleObject() for the one handle that WaitForMultipleObject()
    // returned.
    DWORD timeout =
        poll_timeout_ms >= 0 ? static_cast<DWORD>(poll_timeout_ms) : INFINITE;
    windows_wait_res =
        WaitForMultipleObjects(static_cast<DWORD>(poll_fds_.size()),
                               &poll_fds_[0], /*bWaitAll=*/false, timeout);
#else
    platform::BeforeMaybeBlockingSyscall();
    int ret = PERFETTO_EINTR(poll(
        &poll_fds_[0], static_cast<nfds_t>(poll_fds_.size()), poll_timeout_ms));
    platform::AfterMaybeBlockingSyscall();
    PERFETTO_CHECK(ret >= 0);
#endif
    PostFileDescriptorWatches(windows_wait_res);

    if (immediate_task) {
      errno = 0;
      RunTaskWithWatchdogGuard(std::move(immediate_task));
    }

    if (delayed_task) {
      errno = 0;
      RunTaskWithWatchdogGuard(std::move(delayed_task));
    }
  }

  // Wait for all other threads to have finished the Quit's PostTask().
  // This is to prevent the following race in tests:
  // - Thread1 (!= main thread) invokes Quit, which in turn becomes a PostTask.
  // - This function sees quit_=true and returns from Run().
  // - The owner of the LFTR at that point is entitled to destroy LFTR.
  // - Thread1 is still executing the epilogue of the PostTask, decrementing the
  //   refcount, and ends up operating on invalid memory.
  while (
      std::any_of(refcounts_.begin(), refcounts_.end(),
                  [](std::atomic<int32_t>& bucket) { return bucket.load(); })) {
    std::this_thread::yield();
  }
}

std::function<void()> LockFreeTaskRunner::PopNextImmediateTask() {
  return PopTaskRecursive(tail_.load(), nullptr);
}

std::function<void()> LockFreeTaskRunner::PopTaskRecursive(Slab* slab,
                                                           Slab* next_slab) {
  PERFETTO_DCHECK(RunsTasksOnCurrentThread());
  Slab* prev = slab->prev;
  if (PERFETTO_UNLIKELY(prev)) {
    // In practice it's extemely unlikely that a slab has >1 predecessors.
    // In nominal conditions it is going to have 0 predecessors most of the
    // times and 1 predecessors 1 every kSlabSize times.
    auto task = PopTaskRecursive(prev, slab);
    if (task)
      return task;
  }

  size_t words_fully_consumed = 0;
  for (size_t w = 0; w < Slab::kNumWords; ++w) {
    using BitWord = Slab::BitWord;
    BitWord wr_word = slab->tasks_written[w].load(std::memory_order_acquire);
    BitWord rd_word = slab->tasks_read[w];
    words_fully_consumed += base::AllBitsSet(rd_word) ? 1 : 0;
    BitWord unread_word = wr_word & ~rd_word;

    if (unread_word == 0)
      continue;

    // Find the first unread task in the word.
    uint32_t bit = base::CountTrailZeros(unread_word);
    BitWord bit_mask = BitWord(1) << bit;
    size_t slot = w * Slab::kBitsPerWord + bit;
    std::function<void()> task = std::move(slab->tasks[slot]);
    slab->tasks[slot] = nullptr;
    slab->tasks_read[w] |= bit_mask;
    return task;
  }  // for(word in tasks_written)

  // There are no unconsumed tasks in this Slab. Reached this point, this
  // invocation will return null. However, before doing so, if the slab is fully
  // written (are no slots left) and fully consumed delete it.
  // We never delete the latest slab (the one pointed to by tail_) because
  // we want the tail_ to always point to a valid slab.
  // In principle, we only delete non-tail slabs, and PostTask() threads only
  // access the tail, never walk back on the list, which sounds safe. However
  // there is a potential race we have to guard against:
  // A PostTask() thread could observe Slab 1 (e.g., the only slab), then
  // another PostTask thread() could append (and replace tail_ with) a new
  // Slab 2 (because Slab 1 was full).
  // If Run() comes along and sees the Slab 1 full it will delete it, without
  // realizing that the first thread is still accessing it, thinking it was the
  // tail.
  // Essentially, it is safe to delete non-tail slabs as long as we can
  // guarrantee that no other thread observed that slab through the `tail_` at
  // some point in the recent past. In order to do so, we use a refcount
  // mechanism. If the refcount is 0 we know that no other PostTask thread
  // has loaded the tail, and hence we can safely delete any non-tail slab.
  // If a PostTask thread comes immediately after our check they will observe
  // the new tail (we never delete the tail slab, only the predecessors).

  bool slab_fully_consumed = words_fully_consumed == Slab::kNumWords;

  const uint32_t bucket = HashSlabPtr(slab);
  if (slab_fully_consumed && next_slab && refcounts_[bucket].load() == 0) {
    // NOTE: only the main thread follows the `prev` linked list, writers never
    // look at `prev`. The only contention entrypoint is the `tail_` pointer,
    // which can be modified both by us and by writers.
    PERFETTO_DCHECK(next_slab->prev == slab);
    next_slab->prev = slab->prev;
    slab->prev = nullptr;
    DeleteSlab(slab);
  }

  return nullptr;
}

void LockFreeTaskRunner::Quit() {
  // We have to post the writing of the quit if we are not on the right thread.
  // The alternative would be writing quit_ + WakeUp(), but that is racy:
  // - We write quit_=true from another thread
  // - The main thread sees it immediately (it just happens to be iterating)
  //   and destroys itself.
  // - We try to call WakeUp() while the main thread is being destroyed.
  if (!RunsTasksOnCurrentThread()) {
    PostTask([this] { this->Quit(); });
    return;
  }
  quit_ = true;
}

bool LockFreeTaskRunner::IsIdleForTesting() {
  PERFETTO_DCHECK(RunsTasksOnCurrentThread());
  for (Slab* slab = tail_; slab; slab = slab->prev) {
    for (size_t i = 0; i < Slab::kNumWords; ++i) {
      if (slab->tasks_written[i] & ~slab->tasks_read[i]) {
        return false;
      }
    }
  }
  return true;
}

std::function<void()> LockFreeTaskRunner::PopNextExpiredDelayedTask() {
  PERFETTO_DCHECK(RunsTasksOnCurrentThread());
  TimeMillis now =
      GetWallTimeMs() +
      TimeMillis(advanced_time_for_testing_.load(std::memory_order_relaxed));
  if (!delayed_tasks_.empty() && delayed_tasks_.back().time <= now) {
    auto task = std::move(delayed_tasks_.back().task);
    delayed_tasks_.pop_back();
    return task;
  }
  return nullptr;
}

int LockFreeTaskRunner::GetDelayMsToNextTask() const {
  PERFETTO_DCHECK(RunsTasksOnCurrentThread());
  if (delayed_tasks_.empty()) {
    return -1;  // Forever.
  }
  TimeMillis now =
      GetWallTimeMs() +
      TimeMillis(advanced_time_for_testing_.load(std::memory_order_relaxed));
  TimeMillis deadline = delayed_tasks_.back().time;
  if (deadline <= now) {
    return 0;
  }
  return static_cast<int>((deadline - now).count());
}

LockFreeTaskRunner::Slab* LockFreeTaskRunner::AllocNewSlab() {
  Slab* free_slab = free_slab_.exchange(nullptr);
  if (free_slab) {
    free_slab->~Slab();
    new (free_slab) Slab();
    return free_slab;
  }
  slabs_allocated_.fetch_add(1, std::memory_order_relaxed);
  return new Slab();
}

void LockFreeTaskRunner::DeleteSlab(Slab* slab) {
  PERFETTO_DCHECK(!slab->prev);  // We should never delete a linked slab.
  Slab* null_slab = nullptr;
  if (!free_slab_.compare_exchange_strong(null_slab, slab)) {
    slabs_freed_.fetch_add(1, std::memory_order_relaxed);
    delete slab;
  }
}

void LockFreeTaskRunner::PostDelayedTask(std::function<void()> task,
                                         uint32_t delay_ms) {
  if (!RunsTasksOnCurrentThread()) {
    PostTask([this, task = std::move(task), delay_ms] {
      this->PostDelayedTask(std::move(task), delay_ms);
    });
    return;
  }

  TimeMillis runtime =
      GetWallTimeMs() + TimeMillis(delay_ms) +
      TimeMillis(advanced_time_for_testing_.load(std::memory_order_relaxed));
  delayed_tasks_.insert(
      DelayedTask{runtime, next_delayed_task_seq_++, std::move(task)});
}

void LockFreeTaskRunner::AdvanceTimeForTesting(uint32_t ms) {
  advanced_time_for_testing_.fetch_add(ms);
  WakeUp();
}

void LockFreeTaskRunner::PostFileDescriptorWatches(
    uint64_t windows_wait_result) {
  PERFETTO_DCHECK(RunsTasksOnCurrentThread());
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
    if (handle == wakeup_event_.fd()) {
      wakeup_event_.Clear();
      continue;
    }

    // Binding to |this| is safe since we are the only object executing the
    // task.
    PostTask([this, handle] { this->RunFileDescriptorWatch(handle); });

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

void LockFreeTaskRunner::RunFileDescriptorWatch(PlatformHandle fd) {
  PERFETTO_DCHECK(RunsTasksOnCurrentThread());

  std::function<void()> task;
  auto it = watch_tasks_.find(fd);
  if (it == watch_tasks_.end())
    return;
  WatchTask& watch_task = it->second;

  // Make poll(2) pay attention to the fd again. Since another thread may have
  // updated this watch we need to refresh the set first.
  UpdateWatchTasks();

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
  errno = 0;
  RunTaskWithWatchdogGuard(task);
}

void LockFreeTaskRunner::UpdateWatchTasks() {
  PERFETTO_DCHECK(RunsTasksOnCurrentThread());
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

void LockFreeTaskRunner::AddFileDescriptorWatch(PlatformHandle fd,
                                                std::function<void()> task) {
  PERFETTO_DCHECK(PlatformHandleChecker::IsValid(fd));

  if (!RunsTasksOnCurrentThread()) {
    PostTask([this, task = std::move(task), fd] {
      this->AddFileDescriptorWatch(fd, std::move(task));
    });
    return;
  }

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

void LockFreeTaskRunner::RemoveFileDescriptorWatch(PlatformHandle fd) {
  if (!RunsTasksOnCurrentThread()) {
    PostTask([this, fd] { this->RemoveFileDescriptorWatch(fd); });
    return;
  }

  PERFETTO_DCHECK(watch_tasks_.count(fd));
  watch_tasks_.erase(fd);
  watch_tasks_changed_ = true;
}

bool LockFreeTaskRunner::RunsTasksOnCurrentThread() const {
  return run_task_thread_id_ == std::this_thread::get_id();
}

}  // namespace base
}  // namespace perfetto
