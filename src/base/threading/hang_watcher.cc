// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/hang_watcher.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"

namespace base {

namespace {
HangWatcher* g_instance = nullptr;
}

HangWatchScope::HangWatchScope(TimeDelta timeout) {
  internal::HangWatchState* current_hang_watch_state =
      internal::HangWatchState::GetHangWatchStateForCurrentThread()->Get();

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

HangWatcher::HangWatcher(RepeatingClosure on_hang_closure)
    : on_hang_closure_(std::move(on_hang_closure)) {
  DCHECK(!g_instance);
  g_instance = this;
}

HangWatcher::~HangWatcher() {
  DCHECK_EQ(g_instance, this);
  DCHECK(watch_states_.empty());
  g_instance = nullptr;
}

// static
HangWatcher* HangWatcher::GetInstance() {
  return g_instance;
}

ScopedClosureRunner HangWatcher::RegisterThread() {
  AutoLock auto_lock(watch_state_lock_);

  watch_states_.push_back(
      internal::HangWatchState::CreateHangWatchStateForCurrentThread());

  return ScopedClosureRunner(BindOnce(&HangWatcher::UnregisterThread,
                                      Unretained(HangWatcher::GetInstance())));
}

void HangWatcher::Monitor() {
  bool must_invoke_hang_closure = false;
  {
    AutoLock auto_lock(watch_state_lock_);
    for (const auto& watch_state : watch_states_) {
      if (watch_state->IsOverDeadline()) {
        must_invoke_hang_closure = true;
        break;
      }
    }
  }

  if (must_invoke_hang_closure) {
    // Invoke the closure outside the scope of |watch_state_lock_|
    // to prevent lock reentrancy.
    on_hang_closure_.Run();
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
HangWatchState::HangWatchState() : deadline_(TimeTicks::Max()) {}

HangWatchState::~HangWatchState() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

#if DCHECK_IS_ON()
  // Destroying the HangWatchState should not be done if there are live
  // HangWatchScopes.
  DCHECK(!current_hang_watch_scope_);
#endif
}

// static
std::unique_ptr<HangWatchState>
HangWatchState::CreateHangWatchStateForCurrentThread() {
  // There should not exist a state object for this thread already.
  DCHECK(!GetHangWatchStateForCurrentThread()->Get());

  // Allocate a watch state object for this thread.
  std::unique_ptr<HangWatchState> hang_state =
      std::make_unique<HangWatchState>();

  // Bind the new instance to this thread.
  GetHangWatchStateForCurrentThread()->Set(hang_state.get());

  // Setting the thread local worked.
  DCHECK(GetHangWatchStateForCurrentThread()->Get());

  // Transfer ownership to caller.
  return hang_state;
}

TimeTicks HangWatchState::GetDeadline() const {
  return deadline_.load();
}

TimeTicks HangWatchState::SetDeadline(TimeTicks deadline) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return deadline_.exchange(deadline);
}

bool HangWatchState::IsOverDeadline() const {
  return TimeTicks::Now() > deadline_.load();
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

}  // namespace internal

}  // namespace base
