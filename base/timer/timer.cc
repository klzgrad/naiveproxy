// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/timer/timer.h"

#include <stddef.h>

#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/threading/platform_thread.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/tick_clock.h"

namespace base {

// BaseTimerTaskInternal is a simple delegate for scheduling a callback to Timer
// on the current sequence. It also handles the following edge cases:
// - deleted by the task runner.
// - abandoned (orphaned) by Timer.
class BaseTimerTaskInternal {
 public:
  explicit BaseTimerTaskInternal(Timer* timer)
      : timer_(timer) {
  }

  ~BaseTimerTaskInternal() {
    // This task may be getting cleared because the task runner has been
    // destructed.  If so, don't leave Timer with a dangling pointer
    // to this.
    if (timer_)
      timer_->AbandonAndStop();
  }

  void Run() {
    // |timer_| is nullptr if we were abandoned.
    if (!timer_)
      return;

    // |this| will be deleted by the task runner, so Timer needs to forget us:
    timer_->scheduled_task_ = nullptr;

    // Although Timer should not call back into |this|, let's clear |timer_|
    // first to be pedantic.
    Timer* timer = timer_;
    timer_ = nullptr;
    timer->RunScheduledTask();
  }

  // The task remains in the queue, but nothing will happen when it runs.
  void Abandon() { timer_ = nullptr; }

 private:
  Timer* timer_;

  DISALLOW_COPY_AND_ASSIGN(BaseTimerTaskInternal);
};

Timer::Timer(bool retain_user_task, bool is_repeating)
    : Timer(retain_user_task, is_repeating, nullptr) {}

Timer::Timer(bool retain_user_task, bool is_repeating, TickClock* tick_clock)
    : scheduled_task_(nullptr),
      is_repeating_(is_repeating),
      retain_user_task_(retain_user_task),
      tick_clock_(tick_clock),
      is_running_(false) {
  // It is safe for the timer to be created on a different thread/sequence than
  // the one from which the timer APIs are called. The first call to the
  // checker's CalledOnValidSequence() method will re-bind the checker, and
  // later calls will verify that the same task runner is used.
  origin_sequence_checker_.DetachFromSequence();
}

Timer::Timer(const Location& posted_from,
             TimeDelta delay,
             const base::Closure& user_task,
             bool is_repeating)
    : Timer(posted_from, delay, user_task, is_repeating, nullptr) {}

Timer::Timer(const Location& posted_from,
             TimeDelta delay,
             const base::Closure& user_task,
             bool is_repeating,
             TickClock* tick_clock)
    : scheduled_task_(nullptr),
      posted_from_(posted_from),
      delay_(delay),
      user_task_(user_task),
      is_repeating_(is_repeating),
      retain_user_task_(true),
      tick_clock_(tick_clock),
      is_running_(false) {
  // See comment in other constructor.
  origin_sequence_checker_.DetachFromSequence();
}

Timer::~Timer() {
  DCHECK(origin_sequence_checker_.CalledOnValidSequence());
  AbandonAndStop();
}

bool Timer::IsRunning() const {
  DCHECK(origin_sequence_checker_.CalledOnValidSequence());
  return is_running_;
}

TimeDelta Timer::GetCurrentDelay() const {
  DCHECK(origin_sequence_checker_.CalledOnValidSequence());
  return delay_;
}

void Timer::SetTaskRunner(scoped_refptr<SequencedTaskRunner> task_runner) {
  // Do not allow changing the task runner when the Timer is running.
  // Don't check for |origin_sequence_checker_.CalledOnValidSequence()| here to
  // allow the use case of constructing the Timer and immediatetly invoking
  // SetTaskRunner() before starting it (CalledOnValidSequence() would undo the
  // DetachFromSequence() from the constructor). The |!is_running| check kind of
  // verifies the same thing (and TSAN should catch callers that do it wrong but
  // somehow evade all debug checks).
  DCHECK(!is_running_);
  task_runner_.swap(task_runner);
}

void Timer::Start(const Location& posted_from,
                  TimeDelta delay,
                  const base::Closure& user_task) {
  DCHECK(origin_sequence_checker_.CalledOnValidSequence());

  posted_from_ = posted_from;
  delay_ = delay;
  user_task_ = user_task;

  Reset();
}

void Timer::Stop() {
  // TODO(gab): Enable this when it's no longer called racily from
  // RunScheduledTask(): https://crbug.com/587199.
  // DCHECK(origin_sequence_checker_.CalledOnValidSequence());

  is_running_ = false;

  // It's safe to destroy or restart Timer on another sequence after Stop().
  origin_sequence_checker_.DetachFromSequence();

  if (!retain_user_task_)
    user_task_.Reset();
  // No more member accesses here: |this| could be deleted after freeing
  // |user_task_|.
}

void Timer::Reset() {
  DCHECK(origin_sequence_checker_.CalledOnValidSequence());
  DCHECK(!user_task_.is_null());

  // If there's no pending task, start one up and return.
  if (!scheduled_task_) {
    PostNewScheduledTask(delay_);
    return;
  }

  // Set the new |desired_run_time_|.
  if (delay_ > TimeDelta::FromMicroseconds(0))
    desired_run_time_ = Now() + delay_;
  else
    desired_run_time_ = TimeTicks();

  // We can use the existing scheduled task if it arrives before the new
  // |desired_run_time_|.
  if (desired_run_time_ >= scheduled_run_time_) {
    is_running_ = true;
    return;
  }

  // We can't reuse the |scheduled_task_|, so abandon it and post a new one.
  AbandonScheduledTask();
  PostNewScheduledTask(delay_);
}

TimeTicks Timer::Now() const {
  // TODO(gab): Enable this when it's no longer called racily from
  // RunScheduledTask(): https://crbug.com/587199.
  // DCHECK(origin_sequence_checker_.CalledOnValidSequence());
  return tick_clock_ ? tick_clock_->NowTicks() : TimeTicks::Now();
}

void Timer::PostNewScheduledTask(TimeDelta delay) {
  // TODO(gab): Enable this when it's no longer called racily from
  // RunScheduledTask(): https://crbug.com/587199.
  // DCHECK(origin_sequence_checker_.CalledOnValidSequence());
  DCHECK(!scheduled_task_);
  is_running_ = true;
  scheduled_task_ = new BaseTimerTaskInternal(this);
  if (delay > TimeDelta::FromMicroseconds(0)) {
    // TODO(gab): Posting BaseTimerTaskInternal::Run to another sequence makes
    // this code racy. https://crbug.com/587199
    GetTaskRunner()->PostDelayedTask(
        posted_from_,
        base::BindOnce(&BaseTimerTaskInternal::Run,
                       base::Owned(scheduled_task_)),
        delay);
    scheduled_run_time_ = desired_run_time_ = Now() + delay;
  } else {
    GetTaskRunner()->PostTask(posted_from_,
                              base::BindOnce(&BaseTimerTaskInternal::Run,
                                             base::Owned(scheduled_task_)));
    scheduled_run_time_ = desired_run_time_ = TimeTicks();
  }
}

scoped_refptr<SequencedTaskRunner> Timer::GetTaskRunner() {
  return task_runner_.get() ? task_runner_ : SequencedTaskRunnerHandle::Get();
}

void Timer::AbandonScheduledTask() {
  // TODO(gab): Enable this when it's no longer called racily from
  // RunScheduledTask() -> Stop(): https://crbug.com/587199.
  // DCHECK(origin_sequence_checker_.CalledOnValidSequence());
  if (scheduled_task_) {
    scheduled_task_->Abandon();
    scheduled_task_ = nullptr;
  }
}

void Timer::RunScheduledTask() {
  // TODO(gab): Enable this when it's no longer called racily:
  // https://crbug.com/587199.
  // DCHECK(origin_sequence_checker_.CalledOnValidSequence());

  // Task may have been disabled.
  if (!is_running_)
    return;

  // First check if we need to delay the task because of a new target time.
  if (desired_run_time_ > scheduled_run_time_) {
    // Now() can be expensive, so only call it if we know the user has changed
    // the |desired_run_time_|.
    TimeTicks now = Now();
    // Task runner may have called us late anyway, so only post a continuation
    // task if the |desired_run_time_| is in the future.
    if (desired_run_time_ > now) {
      // Post a new task to span the remaining time.
      PostNewScheduledTask(desired_run_time_ - now);
      return;
    }
  }

  // Make a local copy of the task to run. The Stop method will reset the
  // |user_task_| member if |retain_user_task_| is false.
  base::Closure task = user_task_;

  if (is_repeating_)
    PostNewScheduledTask(delay_);
  else
    Stop();

  task.Run();

  // No more member accesses here: |this| could be deleted at this point.
}

}  // namespace base
