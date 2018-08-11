// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_pump_default.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/message_loop/message_pump_for_ui.h"
#include "base/run_loop.h"
#include "base/third_party/dynamic_annotations/dynamic_annotations.h"
#include "base/threading/thread_id_name_manager.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"

#if defined(OS_MACOSX)
#include "base/message_loop/message_pump_mac.h"
#endif

namespace base {

namespace {

MessageLoop::MessagePumpFactory* message_pump_for_ui_factory_ = nullptr;

std::unique_ptr<MessagePump> ReturnPump(std::unique_ptr<MessagePump> pump) {
  return pump;
}

}  // namespace

//------------------------------------------------------------------------------

MessageLoop::MessageLoop(Type type)
    : MessageLoop(type, MessagePumpFactoryCallback()) {
  BindToCurrentThread();
}

MessageLoop::MessageLoop(std::unique_ptr<MessagePump> pump)
    : MessageLoop(TYPE_CUSTOM, BindOnce(&ReturnPump, std::move(pump))) {
  BindToCurrentThread();
}

MessageLoop::~MessageLoop() {
  // If |pump_| is non-null, this message loop has been bound and should be the
  // current one on this thread. Otherwise, this loop is being destructed before
  // it was bound to a thread, so a different message loop (or no loop at all)
  // may be current.
  DCHECK((pump_ && MessageLoopCurrent::IsBoundToCurrentThreadInternal(this)) ||
         (!pump_ && !MessageLoopCurrent::IsBoundToCurrentThreadInternal(this)));

  // iOS just attaches to the loop, it doesn't Run it.
  // TODO(stuartmorgan): Consider wiring up a Detach().
#if !defined(OS_IOS)
  // There should be no active RunLoops on this thread, unless this MessageLoop
  // isn't bound to the current thread (see other condition at the top of this
  // method).
  DCHECK(
      (!pump_ && !MessageLoopCurrent::IsBoundToCurrentThreadInternal(this)) ||
      !RunLoop::IsRunningOnCurrentThread());
#endif  // !defined(OS_IOS)

#if defined(OS_WIN)
  if (in_high_res_mode_)
    Time::ActivateHighResolutionTimer(false);
#endif
  // Clean up any unprocessed tasks, but take care: deleting a task could
  // result in the addition of more tasks (e.g., via DeleteSoon).  We set a
  // limit on the number of times we will allow a deleted task to generate more
  // tasks.  Normally, we should only pass through this loop once or twice.  If
  // we end up hitting the loop limit, then it is probably due to one task that
  // is being stubborn.  Inspect the queues to see who is left.
  bool tasks_remain;
  for (int i = 0; i < 100; ++i) {
    DeletePendingTasks();
    // If we end up with empty queues, then break out of the loop.
    tasks_remain = incoming_task_queue_->triage_tasks().HasTasks();
    if (!tasks_remain)
      break;
  }
  DCHECK(!tasks_remain);

  // Let interested parties have one last shot at accessing this.
  for (auto& observer : destruction_observers_)
    observer.WillDestroyCurrentMessageLoop();

  thread_task_runner_handle_.reset();

  // Tell the incoming queue that we are dying.
  incoming_task_queue_->WillDestroyCurrentMessageLoop();
  incoming_task_queue_ = nullptr;
  unbound_task_runner_ = nullptr;
  task_runner_ = nullptr;

  // OK, now make it so that no one can find us.
  if (MessageLoopCurrent::IsBoundToCurrentThreadInternal(this))
    MessageLoopCurrent::UnbindFromCurrentThreadInternal(this);
}

// static
MessageLoopCurrent MessageLoop::current() {
  return MessageLoopCurrent::Get();
}

// static
bool MessageLoop::InitMessagePumpForUIFactory(MessagePumpFactory* factory) {
  if (message_pump_for_ui_factory_)
    return false;

  message_pump_for_ui_factory_ = factory;
  return true;
}

// static
std::unique_ptr<MessagePump> MessageLoop::CreateMessagePumpForType(Type type) {
  if (type == MessageLoop::TYPE_UI) {
    if (message_pump_for_ui_factory_)
      return message_pump_for_ui_factory_();
#if defined(OS_IOS) || defined(OS_MACOSX)
    return MessagePumpMac::Create();
#elif defined(OS_NACL) || defined(OS_AIX)
    // Currently NaCl and AIX don't have a UI MessageLoop.
    // TODO(abarth): Figure out if we need this.
    NOTREACHED();
    return nullptr;
#else
    return std::make_unique<MessagePumpForUI>();
#endif
  }

  if (type == MessageLoop::TYPE_IO)
    return std::unique_ptr<MessagePump>(new MessagePumpForIO());

#if defined(OS_ANDROID)
  if (type == MessageLoop::TYPE_JAVA)
    return std::unique_ptr<MessagePump>(new MessagePumpForUI());
#endif

  DCHECK_EQ(MessageLoop::TYPE_DEFAULT, type);
#if defined(OS_IOS)
  // On iOS, a native runloop is always required to pump system work.
  return std::make_unique<MessagePumpCFRunLoop>();
#else
  return std::make_unique<MessagePumpDefault>();
#endif
}

bool MessageLoop::IsType(Type type) const {
  return type_ == type;
}

// TODO(gab): Migrate TaskObservers to RunLoop as part of separating concerns
// between MessageLoop and RunLoop and making MessageLoop a swappable
// implementation detail. http://crbug.com/703346
void MessageLoop::AddTaskObserver(TaskObserver* task_observer) {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  task_observers_.AddObserver(task_observer);
}

void MessageLoop::RemoveTaskObserver(TaskObserver* task_observer) {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  task_observers_.RemoveObserver(task_observer);
}

bool MessageLoop::IsIdleForTesting() {
  // Have unprocessed tasks? (this reloads the work queue if necessary)
  if (incoming_task_queue_->triage_tasks().HasTasks())
    return false;

  // Have unprocessed deferred tasks which can be processed at this run-level?
  if (incoming_task_queue_->deferred_tasks().HasTasks() &&
      !RunLoop::IsNestedOnCurrentThread()) {
    return false;
  }

  return true;
}

//------------------------------------------------------------------------------

// static
std::unique_ptr<MessageLoop> MessageLoop::CreateUnbound(
    Type type,
    MessagePumpFactoryCallback pump_factory) {
  return WrapUnique(new MessageLoop(type, std::move(pump_factory)));
}

MessageLoop::MessageLoop(Type type, MessagePumpFactoryCallback pump_factory)
    : MessageLoopCurrent(this),
      type_(type),
      pump_factory_(std::move(pump_factory)),
      incoming_task_queue_(new internal::IncomingTaskQueue(this)),
      unbound_task_runner_(
          new internal::MessageLoopTaskRunner(incoming_task_queue_)),
      task_runner_(unbound_task_runner_) {
  // If type is TYPE_CUSTOM non-null pump_factory must be given.
  DCHECK(type_ != TYPE_CUSTOM || !pump_factory_.is_null());

  // Bound in BindToCurrentThread();
  DETACH_FROM_THREAD(bound_thread_checker_);
}

void MessageLoop::BindToCurrentThread() {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);

  DCHECK(!pump_);
  if (!pump_factory_.is_null())
    pump_ = std::move(pump_factory_).Run();
  else
    pump_ = CreateMessagePumpForType(type_);

  DCHECK(!MessageLoopCurrent::IsSet())
      << "should only have one message loop per thread";
  MessageLoopCurrent::BindToCurrentThreadInternal(this);

  incoming_task_queue_->StartScheduling();
  unbound_task_runner_->BindToCurrentThread();
  unbound_task_runner_ = nullptr;
  SetThreadTaskRunnerHandle();
  thread_id_ = PlatformThread::CurrentId();

  scoped_set_sequence_local_storage_map_for_current_thread_ = std::make_unique<
      internal::ScopedSetSequenceLocalStorageMapForCurrentThread>(
      &sequence_local_storage_map_);

  RunLoop::RegisterDelegateForCurrentThread(this);
}

std::string MessageLoop::GetThreadName() const {
  DCHECK_NE(kInvalidThreadId, thread_id_)
      << "GetThreadName() must only be called after BindToCurrentThread()'s "
      << "side-effects have been synchronized with this thread.";
  return ThreadIdNameManager::GetInstance()->GetName(thread_id_);
}

void MessageLoop::SetTaskRunner(
    scoped_refptr<SingleThreadTaskRunner> task_runner) {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);

  DCHECK(task_runner);
  DCHECK(task_runner->BelongsToCurrentThread());
  DCHECK(!unbound_task_runner_);
  task_runner_ = std::move(task_runner);
  SetThreadTaskRunnerHandle();
}

void MessageLoop::ClearTaskRunnerForTesting() {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);

  DCHECK(!unbound_task_runner_);
  task_runner_ = nullptr;
  thread_task_runner_handle_.reset();
}

void MessageLoop::Run(bool application_tasks_allowed) {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  if (application_tasks_allowed && !task_execution_allowed_) {
    // Allow nested task execution as explicitly requested.
    DCHECK(RunLoop::IsNestedOnCurrentThread());
    task_execution_allowed_ = true;
    pump_->Run(this);
    task_execution_allowed_ = false;
  } else {
    pump_->Run(this);
  }
}

void MessageLoop::Quit() {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  pump_->Quit();
}

void MessageLoop::EnsureWorkScheduled() {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  if (incoming_task_queue_->triage_tasks().HasTasks())
    pump_->ScheduleWork();
}

void MessageLoop::SetThreadTaskRunnerHandle() {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  // Clear the previous thread task runner first, because only one can exist at
  // a time.
  thread_task_runner_handle_.reset();
  thread_task_runner_handle_.reset(new ThreadTaskRunnerHandle(task_runner_));
}

bool MessageLoop::ProcessNextDelayedNonNestableTask() {
  if (RunLoop::IsNestedOnCurrentThread())
    return false;

  while (incoming_task_queue_->deferred_tasks().HasTasks()) {
    PendingTask pending_task = incoming_task_queue_->deferred_tasks().Pop();
    if (!pending_task.task.IsCancelled()) {
      RunTask(&pending_task);
      return true;
    }
  }

  return false;
}

void MessageLoop::RunTask(PendingTask* pending_task) {
  DCHECK(task_execution_allowed_);

  // Execute the task and assume the worst: It is probably not reentrant.
  task_execution_allowed_ = false;

  TRACE_TASK_EXECUTION("MessageLoop::RunTask", *pending_task);

  for (auto& observer : task_observers_)
    observer.WillProcessTask(*pending_task);
  incoming_task_queue_->RunTask(pending_task);
  for (auto& observer : task_observers_)
    observer.DidProcessTask(*pending_task);

  task_execution_allowed_ = true;
}

bool MessageLoop::DeferOrRunPendingTask(PendingTask pending_task) {
  if (pending_task.nestable == Nestable::kNestable ||
      !RunLoop::IsNestedOnCurrentThread()) {
    RunTask(&pending_task);
    // Show that we ran a task (Note: a new one might arrive as a
    // consequence!).
    return true;
  }

  // We couldn't run the task now because we're in a nested run loop
  // and the task isn't nestable.
  incoming_task_queue_->deferred_tasks().Push(std::move(pending_task));
  return false;
}

void MessageLoop::DeletePendingTasks() {
  incoming_task_queue_->triage_tasks().Clear();
  incoming_task_queue_->deferred_tasks().Clear();
  // TODO(robliao): Determine if we can move delayed task destruction before
  // deferred tasks to maintain the MessagePump DoWork, DoDelayedWork, and
  // DoIdleWork processing order.
  incoming_task_queue_->delayed_tasks().Clear();
}

void MessageLoop::ScheduleWork() {
  pump_->ScheduleWork();
}

bool MessageLoop::DoWork() {
  if (!task_execution_allowed_)
    return false;

  // Execute oldest task.
  while (incoming_task_queue_->triage_tasks().HasTasks()) {
    PendingTask pending_task = incoming_task_queue_->triage_tasks().Pop();
    if (pending_task.task.IsCancelled())
      continue;

    if (!pending_task.delayed_run_time.is_null()) {
      int sequence_num = pending_task.sequence_num;
      TimeTicks delayed_run_time = pending_task.delayed_run_time;
      incoming_task_queue_->delayed_tasks().Push(std::move(pending_task));
      // If we changed the topmost task, then it is time to reschedule.
      if (incoming_task_queue_->delayed_tasks().Peek().sequence_num ==
          sequence_num) {
        pump_->ScheduleDelayedWork(delayed_run_time);
      }
    } else if (DeferOrRunPendingTask(std::move(pending_task))) {
      return true;
    }
  }

  // Nothing happened.
  return false;
}

bool MessageLoop::DoDelayedWork(TimeTicks* next_delayed_work_time) {
  if (!task_execution_allowed_ ||
      !incoming_task_queue_->delayed_tasks().HasTasks()) {
    recent_time_ = *next_delayed_work_time = TimeTicks();
    return false;
  }

  // When we "fall behind", there will be a lot of tasks in the delayed work
  // queue that are ready to run.  To increase efficiency when we fall behind,
  // we will only call Time::Now() intermittently, and then process all tasks
  // that are ready to run before calling it again.  As a result, the more we
  // fall behind (and have a lot of ready-to-run delayed tasks), the more
  // efficient we'll be at handling the tasks.

  TimeTicks next_run_time =
      incoming_task_queue_->delayed_tasks().Peek().delayed_run_time;
  if (next_run_time > recent_time_) {
    recent_time_ = TimeTicks::Now();  // Get a better view of Now();
    if (next_run_time > recent_time_) {
      *next_delayed_work_time = next_run_time;
      return false;
    }
  }

  PendingTask pending_task = incoming_task_queue_->delayed_tasks().Pop();

  if (incoming_task_queue_->delayed_tasks().HasTasks()) {
    *next_delayed_work_time =
        incoming_task_queue_->delayed_tasks().Peek().delayed_run_time;
  }

  return DeferOrRunPendingTask(std::move(pending_task));
}

bool MessageLoop::DoIdleWork() {
  if (ProcessNextDelayedNonNestableTask())
    return true;

  if (ShouldQuitWhenIdle())
    pump_->Quit();

  // When we return we will do a kernel wait for more tasks.
#if defined(OS_WIN)
  // On Windows we activate the high resolution timer so that the wait
  // _if_ triggered by the timer happens with good resolution. If we don't
  // do this the default resolution is 15ms which might not be acceptable
  // for some tasks.
  bool high_res = incoming_task_queue_->HasPendingHighResolutionTasks();
  if (high_res != in_high_res_mode_) {
    in_high_res_mode_ = high_res;
    Time::ActivateHighResolutionTimer(in_high_res_mode_);
  }
#endif
  return false;
}

#if !defined(OS_NACL)

//------------------------------------------------------------------------------
// MessageLoopForUI

MessageLoopForUI::MessageLoopForUI(std::unique_ptr<MessagePump> pump)
    : MessageLoop(TYPE_UI, BindOnce(&ReturnPump, std::move(pump))) {}

// static
MessageLoopCurrentForUI MessageLoopForUI::current() {
  return MessageLoopCurrentForUI::Get();
}

// static
bool MessageLoopForUI::IsCurrent() {
  return MessageLoopCurrentForUI::IsSet();
}

#if defined(OS_IOS)
void MessageLoopForUI::Attach() {
  static_cast<MessagePumpUIApplication*>(pump_.get())->Attach(this);
}
#endif  // defined(OS_IOS)

#if defined(OS_ANDROID)
void MessageLoopForUI::Start() {
  // No Histogram support for UI message loop as it is managed by Java side
  static_cast<MessagePumpForUI*>(pump_.get())->Start(this);
}

void MessageLoopForUI::Abort() {
  static_cast<MessagePumpForUI*>(pump_.get())->Abort();
}
#endif  // defined(OS_ANDROID)

#if defined(OS_WIN)
void MessageLoopForUI::EnableWmQuit() {
  static_cast<MessagePumpForUI*>(pump_.get())->EnableWmQuit();
}
#endif  // defined(OS_WIN)

#endif  // !defined(OS_NACL)

//------------------------------------------------------------------------------
// MessageLoopForIO

// static
MessageLoopCurrentForIO MessageLoopForIO::current() {
  return MessageLoopCurrentForIO::Get();
}

// static
bool MessageLoopForIO::IsCurrent() {
  return MessageLoopCurrentForIO::IsSet();
}

}  // namespace base
