// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/sequence_manager/task_queue.h"

#include <utility>

#include "base/bind.h"
#include "base/task/sequence_manager/associated_thread_id.h"
#include "base/task/sequence_manager/sequence_manager_impl.h"
#include "base/task/sequence_manager/task_queue_impl.h"
#include "base/task/sequence_manager/task_queue_task_runner.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"

namespace base {
namespace sequence_manager {

TaskQueue::TaskQueue(std::unique_ptr<internal::TaskQueueImpl> impl,
                     const TaskQueue::Spec& spec)
    : impl_(std::move(impl)),
      sequence_manager_(impl_ ? impl_->GetSequenceManagerWeakPtr() : nullptr),
      graceful_queue_shutdown_helper_(
          impl_ ? impl_->GetGracefulQueueShutdownHelper() : nullptr),
      associated_thread_((impl_ && impl_->sequence_manager())
                             ? impl_->sequence_manager()->associated_thread()
                             : MakeRefCounted<internal::AssociatedThreadId>()) {
}

TaskQueue::~TaskQueue() {
  // scoped_refptr guarantees us that this object isn't used.
  if (!impl_)
    return;
  if (impl_->IsUnregistered())
    return;
  graceful_queue_shutdown_helper_->GracefullyShutdownTaskQueue(
      TakeTaskQueueImpl());
}

TaskQueue::Task::Task(TaskQueue::PostedTask task, TimeTicks desired_run_time)
    : PendingTask(task.posted_from,
                  std::move(task.callback),
                  desired_run_time,
                  task.nestable),
      task_type_(task.task_type) {}

TaskQueue::TaskTiming::TaskTiming(bool has_wall_time, bool has_thread_time)
    : has_wall_time_(has_wall_time), has_thread_time_(has_thread_time) {}

void TaskQueue::TaskTiming::RecordTaskStart(LazyNow* now) {
  if (has_wall_time())
    start_time_ = now->Now();
  if (has_thread_time())
    start_thread_time_ = base::ThreadTicks::Now();
}

void TaskQueue::TaskTiming::RecordTaskEnd(LazyNow* now) {
  if (has_wall_time())
    end_time_ = now->Now();
  if (has_thread_time())
    end_thread_time_ = base::ThreadTicks::Now();
}

TaskQueue::PostedTask::PostedTask(OnceClosure callback,
                                  Location posted_from,
                                  TimeDelta delay,
                                  Nestable nestable,
                                  int task_type)
    : callback(std::move(callback)),
      posted_from(posted_from),
      delay(delay),
      nestable(nestable),
      task_type(task_type) {}

TaskQueue::PostedTask::PostedTask(PostedTask&& move_from)
    : callback(std::move(move_from.callback)),
      posted_from(move_from.posted_from),
      delay(move_from.delay),
      nestable(move_from.nestable),
      task_type(move_from.task_type) {}

TaskQueue::PostedTask::~PostedTask() = default;

void TaskQueue::ShutdownTaskQueue() {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  AutoLock lock(impl_lock_);
  if (!impl_)
    return;
  if (!sequence_manager_) {
    impl_.reset();
    return;
  }
  impl_->SetBlameContext(nullptr);
  impl_->SetOnTaskStartedHandler(
      internal::TaskQueueImpl::OnTaskStartedHandler());
  impl_->SetOnTaskCompletedHandler(
      internal::TaskQueueImpl::OnTaskCompletedHandler());
  sequence_manager_->UnregisterTaskQueueImpl(TakeTaskQueueImpl());
}

scoped_refptr<SingleThreadTaskRunner> TaskQueue::CreateTaskRunner(
    int task_type) {
  return MakeRefCounted<internal::TaskQueueTaskRunner>(this, task_type);
}

bool TaskQueue::RunsTasksInCurrentSequence() const {
  return IsOnMainThread();
}

bool TaskQueue::PostDelayedTask(const Location& from_here,
                                OnceClosure task,
                                TimeDelta delay) {
  return PostTaskWithMetadata(
      PostedTask(std::move(task), from_here, delay, Nestable::kNestable));
}

bool TaskQueue::PostNonNestableDelayedTask(const Location& from_here,
                                           OnceClosure task,
                                           TimeDelta delay) {
  return PostTaskWithMetadata(
      PostedTask(std::move(task), from_here, delay, Nestable::kNonNestable));
}

bool TaskQueue::PostTaskWithMetadata(PostedTask task) {
  Optional<MoveableAutoLock> lock = AcquireImplReadLockIfNeeded();
  if (!impl_)
    return false;
  internal::TaskQueueImpl::PostTaskResult result(
      impl_->PostDelayedTask(std::move(task)));
  if (result.success)
    return true;
  // If posting task was unsuccessful then |result| will contain
  // the original task which should be destructed outside of the lock.
  lock = nullopt;
  // Task gets implicitly destructed here.
  return false;
}

std::unique_ptr<TaskQueue::QueueEnabledVoter>
TaskQueue::CreateQueueEnabledVoter() {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return nullptr;
  return impl_->CreateQueueEnabledVoter(this);
}

bool TaskQueue::IsQueueEnabled() const {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return false;
  return impl_->IsQueueEnabled();
}

bool TaskQueue::IsEmpty() const {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return true;
  return impl_->IsEmpty();
}

size_t TaskQueue::GetNumberOfPendingTasks() const {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return 0;
  return impl_->GetNumberOfPendingTasks();
}

bool TaskQueue::HasTaskToRunImmediately() const {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return false;
  return impl_->HasTaskToRunImmediately();
}

Optional<TimeTicks> TaskQueue::GetNextScheduledWakeUp() {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return nullopt;
  return impl_->GetNextScheduledWakeUp();
}

void TaskQueue::SetQueuePriority(TaskQueue::QueuePriority priority) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return;
  impl_->SetQueuePriority(priority);
}

TaskQueue::QueuePriority TaskQueue::GetQueuePriority() const {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return TaskQueue::QueuePriority::kLowPriority;
  return impl_->GetQueuePriority();
}

void TaskQueue::AddTaskObserver(MessageLoop::TaskObserver* task_observer) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return;
  impl_->AddTaskObserver(task_observer);
}

void TaskQueue::RemoveTaskObserver(MessageLoop::TaskObserver* task_observer) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return;
  impl_->RemoveTaskObserver(task_observer);
}

void TaskQueue::SetTimeDomain(TimeDomain* time_domain) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return;
  impl_->SetTimeDomain(time_domain);
}

TimeDomain* TaskQueue::GetTimeDomain() const {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return nullptr;
  return impl_->GetTimeDomain();
}

void TaskQueue::SetBlameContext(trace_event::BlameContext* blame_context) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return;
  impl_->SetBlameContext(blame_context);
}

void TaskQueue::InsertFence(InsertFencePosition position) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return;
  impl_->InsertFence(position);
}

void TaskQueue::InsertFenceAt(TimeTicks time) {
  impl_->InsertFenceAt(time);
}

void TaskQueue::RemoveFence() {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return;
  impl_->RemoveFence();
}

bool TaskQueue::HasActiveFence() {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return false;
  return impl_->HasActiveFence();
}

bool TaskQueue::BlockedByFence() const {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return false;
  return impl_->BlockedByFence();
}

const char* TaskQueue::GetName() const {
  auto lock = AcquireImplReadLockIfNeeded();
  if (!impl_)
    return "";
  return impl_->GetName();
}

void TaskQueue::SetObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (!impl_)
    return;
  if (observer) {
    // Observer is guaranteed to outlive TaskQueue and TaskQueueImpl lifecycle
    // is controlled by |this|.
    impl_->SetOnNextWakeUpChangedCallback(
        BindRepeating(&TaskQueue::Observer::OnQueueNextWakeUpChanged,
                      Unretained(observer), Unretained(this)));
  } else {
    impl_->SetOnNextWakeUpChangedCallback(RepeatingCallback<void(TimeTicks)>());
  }
}

bool TaskQueue::IsOnMainThread() const {
  return associated_thread_->thread_id == PlatformThread::CurrentId();
}

Optional<MoveableAutoLock> TaskQueue::AcquireImplReadLockIfNeeded() const {
  if (IsOnMainThread())
    return nullopt;
  return MoveableAutoLock(impl_lock_);
}

std::unique_ptr<internal::TaskQueueImpl> TaskQueue::TakeTaskQueueImpl() {
  DCHECK(impl_);
  return std::move(impl_);
}

}  // namespace sequence_manager
}  // namespace base
