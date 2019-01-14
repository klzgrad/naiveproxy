// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/sequence_manager/test/lazy_thread_controller_for_test.h"

#include "base/message_loop/message_loop.h"
#include "base/time/default_tick_clock.h"

namespace base {
namespace sequence_manager {

LazyThreadControllerForTest::LazyThreadControllerForTest()
    : ThreadControllerImpl(MessageLoop::current(),
                           nullptr,
                           DefaultTickClock::GetInstance()),
      thread_ref_(PlatformThread::CurrentRef()) {
  if (message_loop_)
    task_runner_ = message_loop_->task_runner();
}

LazyThreadControllerForTest::~LazyThreadControllerForTest() = default;

void LazyThreadControllerForTest::EnsureMessageLoop() {
  if (message_loop_)
    return;
  DCHECK(RunsTasksInCurrentSequence());
  message_loop_ = MessageLoop::current();
  DCHECK(message_loop_);
  task_runner_ = message_loop_->task_runner();
  if (pending_observer_) {
    RunLoop::AddNestingObserverOnCurrentThread(this);
    pending_observer_ = false;
  }
  if (pending_default_task_runner_) {
    ThreadControllerImpl::SetDefaultTaskRunner(pending_default_task_runner_);
    pending_default_task_runner_ = nullptr;
  }
}

bool LazyThreadControllerForTest::HasMessageLoop() {
  return !!message_loop_;
}

void LazyThreadControllerForTest::AddNestingObserver(
    RunLoop::NestingObserver* observer) {
  // While |observer| _could_ be associated with the current thread regardless
  // of the presence of a MessageLoop, the association is delayed until
  // EnsureMessageLoop() is invoked. This works around a state issue where
  // otherwise many tests fail because of the following sequence:
  //   1) blink::scheduler::CreateRendererSchedulerForTests()
  //       -> SequenceManager::SequenceManager()
  //       -> LazySchedulerMessageLoopDelegateForTests::AddNestingObserver()
  //   2) Any test framework with a MessageLoop member (and not caring
  //      about the blink scheduler) does:
  //        blink::scheduler::GetSingleThreadTaskRunnerForTesting()->PostTask(
  //            FROM_HERE, an_init_task_with_a_nested_loop);
  //        RunLoop.RunUntilIdle();
  //   3) |a_task_with_a_nested_loop| triggers
  //          SequenceManager::OnBeginNestedLoop() which:
  //            a) flags any_thread().is_nested = true;
  //            b) posts a task to self, which triggers:
  //                 LazySchedulerMessageLoopDelegateForTests::PostDelayedTask()
  //   4) This self-task in turn triggers SequenceManager::DoWork()
  //      which expects to be the only one to trigger nested loops (doesn't
  //      support SequenceManager::OnBeginNestedLoop() being invoked before
  //      it kicks in), resulting in it hitting:
  //      DCHECK_EQ(any_thread().is_nested, delegate_->IsNested()); (1 vs 0).
  // TODO(skyostil): fix this convolution as part of http://crbug.com/495659.
  ThreadControllerImpl::nesting_observer_ = observer;
  if (!HasMessageLoop()) {
    DCHECK(!pending_observer_);
    pending_observer_ = true;
    return;
  }
  RunLoop::AddNestingObserverOnCurrentThread(this);
}

void LazyThreadControllerForTest::RemoveNestingObserver(
    RunLoop::NestingObserver* observer) {
  ThreadControllerImpl::nesting_observer_ = nullptr;
  if (!HasMessageLoop()) {
    DCHECK(pending_observer_);
    pending_observer_ = false;
    return;
  }
  if (MessageLoop::current() != message_loop_)
    return;
  RunLoop::RemoveNestingObserverOnCurrentThread(this);
}

bool LazyThreadControllerForTest::RunsTasksInCurrentSequence() {
  return thread_ref_ == PlatformThread::CurrentRef();
}

void LazyThreadControllerForTest::ScheduleWork() {
  EnsureMessageLoop();
  ThreadControllerImpl::ScheduleWork();
}

void LazyThreadControllerForTest::SetNextDelayedDoWork(LazyNow* lazy_now,
                                                       TimeTicks run_time) {
  EnsureMessageLoop();
  ThreadControllerImpl::SetNextDelayedDoWork(lazy_now, run_time);
}

void LazyThreadControllerForTest::SetDefaultTaskRunner(
    scoped_refptr<SingleThreadTaskRunner> task_runner) {
  if (!HasMessageLoop()) {
    pending_default_task_runner_ = task_runner;
    return;
  }
  ThreadControllerImpl::SetDefaultTaskRunner(task_runner);
}

void LazyThreadControllerForTest::RestoreDefaultTaskRunner() {
  pending_default_task_runner_ = nullptr;
  if (HasMessageLoop() && MessageLoop::current() == message_loop_)
    ThreadControllerImpl::RestoreDefaultTaskRunner();
}

}  // namespace sequence_manager
}  // namespace base
