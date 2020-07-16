// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/hang_watcher.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/debug/alias.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/feature_list.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace base {

// static
constexpr base::Feature HangWatcher::kEnableHangWatcher{
    "EnableHangWatcher", base::FEATURE_DISABLED_BY_DEFAULT};

constexpr base::TimeDelta HangWatchScope::kDefaultHangWatchTime =
    base::TimeDelta::FromSeconds(10);

namespace {
HangWatcher* g_instance = nullptr;
}

constexpr const char* kThreadName = "HangWatcher";

// The time that the HangWatcher thread will sleep for between calls to
// Monitor(). Increasing or decreasing this does not modify the type of hangs
// that can be detected. It instead increases the probability that a call to
// Monitor() will happen at the right time to catch a hang. This has to be
// balanced with power/cpu use concerns as busy looping would catch amost all
// hangs but present unacceptable overhead.
const base::TimeDelta kMonitoringPeriod = base::TimeDelta::FromSeconds(10);

HangWatchScope::HangWatchScope(TimeDelta timeout) {
  internal::HangWatchState* current_hang_watch_state =
      internal::HangWatchState::GetHangWatchStateForCurrentThread()->Get();

  DCHECK(timeout >= base::TimeDelta()) << "Negative timeouts are invalid.";

  // TODO(crbug.com/1034046): Remove when all threads using HangWatchScope are
  // monitored. Thread is not monitored, noop.
  if (!current_hang_watch_state) {
    return;
  }

  DCHECK(current_hang_watch_state)
      << "A scope can only be used on a thread that "
         "registered for hang watching with HangWatcher::RegisterThread.";

#if DCHECK_IS_ON()
  previous_scope_ = current_hang_watch_state->GetCurrentHangWatchScope();
  current_hang_watch_state->SetCurrentHangWatchScope(this);
#endif

  // TODO(crbug.com/1034046): Check whether we are over deadline already for the
  // previous scope here by issuing only one TimeTicks::Now() and resuing the
  // value.

  previous_deadline_ = current_hang_watch_state->GetDeadline();
  TimeTicks deadline = TimeTicks::Now() + timeout;
  current_hang_watch_state->SetDeadline(deadline);
}

HangWatchScope::~HangWatchScope() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  internal::HangWatchState* current_hang_watch_state =
      internal::HangWatchState::GetHangWatchStateForCurrentThread()->Get();

  // TODO(crbug.com/1034046): Remove when all threads using HangWatchScope are
  // monitored. Thread is not monitored, noop.
  if (!current_hang_watch_state) {
    return;
  }

  // If a hang is currently being captured we should block here so execution
  // stops and the relevant stack frames are recorded.
  base::HangWatcher::GetInstance()->BlockIfCaptureInProgress();

#if DCHECK_IS_ON()
  // Verify that no Scope was destructed out of order.
  DCHECK_EQ(this, current_hang_watch_state->GetCurrentHangWatchScope());
  current_hang_watch_state->SetCurrentHangWatchScope(previous_scope_);
#endif

  // Reset the deadline to the value it had before entering this scope.
  current_hang_watch_state->SetDeadline(previous_deadline_);
  // TODO(crbug.com/1034046): Log when a HangWatchScope exits after its deadline
  // and that went undetected by the HangWatcher.
}

HangWatcher::HangWatcher()
    : monitor_period_(kMonitoringPeriod),
      should_monitor_(WaitableEvent::ResetPolicy::AUTOMATIC),
      thread_(this, kThreadName),
      tick_clock_(base::DefaultTickClock::GetInstance()) {
  // |thread_checker_| should not be bound to the constructing thread.
  DETACH_FROM_THREAD(hang_watcher_thread_checker_);

  should_monitor_.declare_only_used_while_idle();

  DCHECK(!g_instance);
  g_instance = this;
  Start();
}

HangWatcher::~HangWatcher() {
  DCHECK_EQ(g_instance, this);
  DCHECK(watch_states_.empty());
  g_instance = nullptr;
  Stop();
}

void HangWatcher::Start() {
  thread_.Start();
}

void HangWatcher::Stop() {
  keep_monitoring_.store(false, std::memory_order_relaxed);
  should_monitor_.Signal();
  thread_.Join();
}

bool HangWatcher::IsWatchListEmpty() {
  AutoLock auto_lock(watch_state_lock_);
  return watch_states_.empty();
}

void HangWatcher::Wait() {
  while (true) {
    // Amount by which the actual time spent sleeping can deviate from
    // the target time and still be considered timely.
    constexpr base::TimeDelta wait_drift_tolerance =
        base::TimeDelta::FromMilliseconds(100);

    base::TimeTicks time_before_wait = tick_clock_->NowTicks();

    // Sleep until next scheduled monitoring or until signaled.
    bool was_signaled = should_monitor_.TimedWait(monitor_period_);

    if (after_wait_callback_) {
      after_wait_callback_.Run(time_before_wait);
    }

    base::TimeTicks time_after_wait = tick_clock_->NowTicks();
    base::TimeDelta wait_time = time_after_wait - time_before_wait;
    bool wait_was_normal =
        wait_time <= (monitor_period_ + wait_drift_tolerance);

    if (!wait_was_normal) {
      // If the time spent waiting was too high it might indicate the machine is
      // very slow or that that it went to sleep. In any case we can't trust the
      // hang watch scopes that are currently live. Update the ignore threshold
      // to make sure they don't trigger a hang on subsequent monitors then keep
      // waiting.

      base::AutoLock auto_lock(watch_state_lock_);

      // Find the latest deadline among the live watch states. They might change
      // atomically while iterating but that's fine because if they do that
      // means the new HangWatchScope was constructed very soon after the
      // abnormal sleep happened and might be affected by the root cause still.
      // Ignoring it is cautious and harmless.
      base::TimeTicks latest_deadline;
      for (const auto& state : watch_states_) {
        base::TimeTicks deadline = state->GetDeadline();
        if (deadline > latest_deadline) {
          latest_deadline = deadline;
        }
      }

      deadline_ignore_threshold_ = latest_deadline;
    }

    // Stop waiting.
    if (wait_was_normal || was_signaled) {
      return;
    }
  }
}

void HangWatcher::Run() {
  // Monitor() should only run on |thread_|. Bind |thread_checker_| here to make
  // sure of that.
  DCHECK_CALLED_ON_VALID_THREAD(hang_watcher_thread_checker_);

  while (keep_monitoring_.load(std::memory_order_relaxed)) {
    // If there is nothing to watch sleep until there is.
    if (IsWatchListEmpty()) {
      should_monitor_.Wait();
    } else {
      Monitor();

      if (after_monitor_closure_for_testing_) {
        after_monitor_closure_for_testing_.Run();
      }
    }

    if (keep_monitoring_.load(std::memory_order_relaxed)) {
      Wait();
    }
  }
}

// static
HangWatcher* HangWatcher::GetInstance() {
  return g_instance;
}

// static
void HangWatcher::RecordHang() {
  base::debug::DumpWithoutCrashing();
  // Inhibit code folding.
  const int line_number = __LINE__;
  base::debug::Alias(&line_number);
}

ScopedClosureRunner HangWatcher::RegisterThread() {
  AutoLock auto_lock(watch_state_lock_);

  watch_states_.push_back(
      internal::HangWatchState::CreateHangWatchStateForCurrentThread());

  // Now that there is a thread to monitor we wake the HangWatcher thread.
  if (watch_states_.size() == 1) {
    should_monitor_.Signal();
  }

  return ScopedClosureRunner(BindOnce(&HangWatcher::UnregisterThread,
                                      Unretained(HangWatcher::GetInstance())));
}

base::TimeTicks HangWatcher::WatchStateSnapShot::GetHighestDeadline() const {
  DCHECK(!hung_watch_state_copies_.empty());
  // Since entries are sorted in increasing order the last entry is the largest
  // one.
  return hung_watch_state_copies_.back().deadline;
}

HangWatcher::WatchStateSnapShot::WatchStateSnapShot(
    const HangWatchStates& watch_states,
    base::TimeTicks snapshot_time,
    base::TimeTicks deadline_ignore_threshold)
    : snapshot_time_(snapshot_time) {
  // Initial copy of the values.
  for (const auto& watch_state : watch_states) {
    base::TimeTicks deadline = watch_state.get()->GetDeadline();

    if (deadline <= deadline_ignore_threshold) {
      hung_watch_state_copies_.clear();
      return;
    }

    // Only copy hung threads.
    if (deadline <= snapshot_time) {
      hung_watch_state_copies_.push_back(
          WatchStateCopy{deadline, watch_state.get()->GetThreadID()});
    }
  }

  // Sort |hung_watch_state_copies_| by order of decreasing hang severity so the
  // most severe hang is first in the list.
  std::sort(hung_watch_state_copies_.begin(), hung_watch_state_copies_.end(),
            [](const WatchStateCopy& lhs, const WatchStateCopy& rhs) {
              return lhs.deadline < rhs.deadline;
            });
}

HangWatcher::WatchStateSnapShot::WatchStateSnapShot(
    const WatchStateSnapShot& other) = default;

HangWatcher::WatchStateSnapShot::~WatchStateSnapShot() = default;

std::string HangWatcher::WatchStateSnapShot::PrepareHungThreadListCrashKey()
    const {
  // Build a crash key string that contains the ids of the hung threads.
  constexpr char kSeparator{'|'};
  std::string list_of_hung_thread_ids;

  // Add as many thread ids to the crash key as possible.
  for (const WatchStateCopy& copy : hung_watch_state_copies_) {
    std::string fragment = base::NumberToString(copy.thread_id) + kSeparator;
    if (list_of_hung_thread_ids.size() + fragment.size() <
        static_cast<std::size_t>(debug::CrashKeySize::Size256)) {
      list_of_hung_thread_ids += fragment;
    } else {
      // Respect the by priority ordering of thread ids in the crash key by
      // stopping the construction as soon as one does not fit. This avoids
      // including lesser priority ids while omitting more important ones.
      break;
    }
  }

  return list_of_hung_thread_ids;
}

HangWatcher::WatchStateSnapShot HangWatcher::GrabWatchStateSnapshotForTesting()
    const {
  WatchStateSnapShot snapshot(watch_states_, base::TimeTicks::Now(),
                              deadline_ignore_threshold_);
  return snapshot;
}

void HangWatcher::Monitor() {
  DCHECK_CALLED_ON_VALID_THREAD(hang_watcher_thread_checker_);
  AutoLock auto_lock(watch_state_lock_);

  // If all threads unregistered since this function was invoked there's
  // nothing to do anymore.
  if (watch_states_.empty())
    return;

  const base::TimeTicks now = base::TimeTicks::Now();

  // See if any thread hung. We're holding |watch_state_lock_| so threads
  // can't register or unregister but their deadline still can change
  // atomically. This is fine. Detecting a hang is generally best effort and
  // if a thread resumes from hang in the time it takes to move on to
  // capturing then its ID will be absent from the crash keys.
  bool any_thread_hung = std::any_of(
      watch_states_.cbegin(), watch_states_.cend(),
      [this, now](const std::unique_ptr<internal::HangWatchState>& state) {
        base::TimeTicks deadline = state->GetDeadline();
        return deadline > deadline_ignore_threshold_ && deadline < now;
      });

  // If at least a thread is hung we need to capture.
  if (any_thread_hung)
    CaptureHang(now);
}

void HangWatcher::CaptureHang(base::TimeTicks capture_time) {
  capture_in_progress.store(true, std::memory_order_relaxed);
  base::AutoLock scope_lock(capture_lock_);

  WatchStateSnapShot watch_state_snapshot(watch_states_, capture_time,
                                          deadline_ignore_threshold_);

  // The hung thread(s) could detected at the start of Monitor() could have
  // moved on from their scopes. If that happened and there are no more hung
  // threads then abort capture.
  std::string list_of_hung_thread_ids =
      watch_state_snapshot.PrepareHungThreadListCrashKey();
  if (list_of_hung_thread_ids.empty())
    return;

#if not defined(OS_NACL)
  static debug::CrashKeyString* crash_key = AllocateCrashKeyString(
      "list-of-hung-threads", debug::CrashKeySize::Size256);
  debug::ScopedCrashKeyString list_of_hung_threads_crash_key_string(
      crash_key, list_of_hung_thread_ids);
#endif

  // To avoid capturing more than one hang that blames a subset of the same
  // threads it's necessary to keep track of what is the furthest deadline
  // that contributed to declaring a hang. Only once
  // all threads have deadlines past this point can we be sure that a newly
  // discovered hang is not directly related.
  // Example:
  // **********************************************************************
  // Timeline A : L------1-------2----------3-------4----------N-----------
  // Timeline B : -------2----------3-------4----------L----5------N-------
  // Timeline C : L----------------------------5------6----7---8------9---N
  // **********************************************************************
  // In the example when a Monitor() happens during timeline A
  // |deadline_ignore_threshold_| (L) is at time zero and deadlines (1-4)
  // are before Now() (N) . A hang is captured and L is updated. During
  // the next Monitor() (timeline B) a new deadline is over but we can't
  // capture a hang because deadlines 2-4 are still live and already counted
  // toward a hang. During a third monitor (timeline C) all live deadlines
  // are now after L and a second hang can be recorded.
  base::TimeTicks latest_expired_deadline =
      watch_state_snapshot.GetHighestDeadline();

  if (on_hang_closure_for_testing_)
    on_hang_closure_for_testing_.Run();
  else
    RecordHang();

  // Update after running the actual capture.
  deadline_ignore_threshold_ = latest_expired_deadline;

  capture_in_progress.store(false, std::memory_order_relaxed);
}

void HangWatcher::SetAfterMonitorClosureForTesting(
    base::RepeatingClosure closure) {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  after_monitor_closure_for_testing_ = std::move(closure);
}

void HangWatcher::SetOnHangClosureForTesting(base::RepeatingClosure closure) {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  on_hang_closure_for_testing_ = std::move(closure);
}

void HangWatcher::SetMonitoringPeriodForTesting(base::TimeDelta period) {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  monitor_period_ = period;
}

void HangWatcher::SetAfterWaitCallbackForTesting(
    RepeatingCallback<void(TimeTicks)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  after_wait_callback_ = callback;
}

void HangWatcher::SignalMonitorEventForTesting() {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  should_monitor_.Signal();
}

void HangWatcher::StopMonitoringForTesting() {
  keep_monitoring_.store(false, std::memory_order_relaxed);
}

void HangWatcher::SetTickClockForTesting(const base::TickClock* tick_clock) {
  tick_clock_ = tick_clock;
}

void HangWatcher::BlockIfCaptureInProgress() {
  // Makes a best-effort attempt to block execution if a hang is currently being
  // captured.Only block on |capture_lock| if |capture_in_progress| hints that
  // it's already held to avoid serializing all threads on this function when no
  // hang capture is in-progress.
  if (capture_in_progress.load(std::memory_order_relaxed)) {
    base::AutoLock hang_lock(capture_lock_);
  }
}

void HangWatcher::UnregisterThread() {
  AutoLock auto_lock(watch_state_lock_);

  internal::HangWatchState* current_hang_watch_state =
      internal::HangWatchState::GetHangWatchStateForCurrentThread()->Get();

  auto it =
      std::find_if(watch_states_.cbegin(), watch_states_.cend(),
                   [current_hang_watch_state](
                       const std::unique_ptr<internal::HangWatchState>& state) {
                     return state.get() == current_hang_watch_state;
                   });

  // Thread should be registered to get unregistered.
  DCHECK(it != watch_states_.end());

  watch_states_.erase(it);
}

namespace internal {

// |deadline_| starts at Max() to avoid validation problems
// when setting the first legitimate value.
HangWatchState::HangWatchState() : thread_id_(PlatformThread::CurrentId()) {
  // There should not exist a state object for this thread already.
  DCHECK(!GetHangWatchStateForCurrentThread()->Get());

  // Bind the new instance to this thread.
  GetHangWatchStateForCurrentThread()->Set(this);
}

HangWatchState::~HangWatchState() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  DCHECK_EQ(GetHangWatchStateForCurrentThread()->Get(), this);
  GetHangWatchStateForCurrentThread()->Set(nullptr);

#if DCHECK_IS_ON()
  // Destroying the HangWatchState should not be done if there are live
  // HangWatchScopes.
  DCHECK(!current_hang_watch_scope_);
#endif
}

// static
std::unique_ptr<HangWatchState>
HangWatchState::CreateHangWatchStateForCurrentThread() {

  // Allocate a watch state object for this thread.
  std::unique_ptr<HangWatchState> hang_state =
      std::make_unique<HangWatchState>();

  // Setting the thread local worked.
  DCHECK_EQ(GetHangWatchStateForCurrentThread()->Get(), hang_state.get());

  // Transfer ownership to caller.
  return hang_state;
}

TimeTicks HangWatchState::GetDeadline() const {
  return deadline_.load(std::memory_order_relaxed);
}

void HangWatchState::SetDeadline(TimeTicks deadline) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  deadline_.store(deadline, std::memory_order_relaxed);
}

bool HangWatchState::IsOverDeadline() const {
  return TimeTicks::Now() > deadline_.load(std::memory_order_relaxed);
}

#if DCHECK_IS_ON()
void HangWatchState::SetCurrentHangWatchScope(HangWatchScope* scope) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  current_hang_watch_scope_ = scope;
}

HangWatchScope* HangWatchState::GetCurrentHangWatchScope() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return current_hang_watch_scope_;
}
#endif

// static
ThreadLocalPointer<HangWatchState>*
HangWatchState::GetHangWatchStateForCurrentThread() {
  static NoDestructor<ThreadLocalPointer<HangWatchState>> hang_watch_state;
  return hang_watch_state.get();
}

PlatformThreadId HangWatchState::GetThreadID() const {
  return thread_id_;
}

}  // namespace internal

}  // namespace base
