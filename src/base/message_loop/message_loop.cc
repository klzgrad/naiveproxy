// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/compiler_specific.h"
#include "base/debug/task_annotator.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop_task_runner.h"
#include "base/message_loop/message_pump_default.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/message_loop/message_pump_for_ui.h"
#include "base/message_loop/sequenced_task_source.h"
#include "base/run_loop.h"
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

class MessageLoop::Controller : public SequencedTaskSource::Observer {
 public:
  // Constructs a MessageLoopController which controls |message_loop|, notifying
  // |task_annotator_| when tasks are queued scheduling work on |message_loop|
  // as fits. |message_loop| and |task_annotator_| will not be used after
  // DisconnectFromParent() returns.
  Controller(MessageLoop* message_loop);

  ~Controller() override;

  // SequencedTaskSource::Observer:
  void WillQueueTask(PendingTask* task) final;
  void DidQueueTask(bool was_empty) final;

  void StartScheduling();

  // Disconnects |message_loop_| from this Controller instance (DidQueueTask()
  // will no-op from this point forward).
  void DisconnectFromParent();

  // Shares this Controller's TaskAnnotator with MessageLoop as TaskAnnotator
  // requires DidQueueTask(x)/RunTask(x) to be invoked on the same TaskAnnotator
  // instance.
  debug::TaskAnnotator& task_annotator() { return task_annotator_; }

 private:
  // A TaskAnnotator which is owned by this Controller to be able to use it
  // without locking |message_loop_lock_|. It cannot be owned by MessageLoop
  // because this Controller cannot access |message_loop_| safely without the
  // lock. Note: the TaskAnnotator API itself is thread-safe.
  debug::TaskAnnotator task_annotator_;

  // Lock that serializes |message_loop_->ScheduleWork()| and access to all
  // members below.
  base::Lock message_loop_lock_;

  // Points to this Controller's outer MessageLoop instance. Null after
  // DisconnectFromParent().
  MessageLoop* message_loop_;

  // False until StartScheduling() is called.
  bool is_ready_for_scheduling_ = false;

  // True if DidQueueTask() has been called before StartScheduling(); letting it
  // know whether it needs to ScheduleWork() right away or not.
  bool pending_schedule_work_ = false;

  DISALLOW_COPY_AND_ASSIGN(Controller);
};

MessageLoop::Controller::Controller(MessageLoop* message_loop)
    : message_loop_(message_loop) {}

MessageLoop::Controller::~Controller() {
  DCHECK(!message_loop_)
      << "DisconnectFromParent() needs to be invoked before destruction.";
}

void MessageLoop::Controller::WillQueueTask(PendingTask* task) {
  task_annotator_.WillQueueTask("MessageLoop::PostTask", task);
}

void MessageLoop::Controller::DidQueueTask(bool was_empty) {
  // Avoid locking if we don't need to schedule.
  if (!was_empty)
    return;

  AutoLock auto_lock(message_loop_lock_);

  if (message_loop_ && is_ready_for_scheduling_)
    message_loop_->ScheduleWork();
  else
    pending_schedule_work_ = true;
}

void MessageLoop::Controller::StartScheduling() {
  AutoLock lock(message_loop_lock_);
  DCHECK(message_loop_);
  DCHECK(!is_ready_for_scheduling_);
  is_ready_for_scheduling_ = true;
  if (pending_schedule_work_)
    message_loop_->ScheduleWork();
}

void MessageLoop::Controller::DisconnectFromParent() {
  AutoLock lock(message_loop_lock_);
  message_loop_ = nullptr;
}

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
    tasks_remain = sequenced_task_source_->HasTasks();
    if (!tasks_remain)
      break;
  }
  DCHECK(!tasks_remain);

  // Let interested parties have one last shot at accessing this.
  for (auto& observer : destruction_observers_)
    observer.WillDestroyCurrentMessageLoop();

  thread_task_runner_handle_.reset();

  // Detach this instance's Controller from |this|. After this point,
  // |underlying_task_runner_| may still receive tasks and notify the controller
  // but the controller will no-op (and not use this MessageLoop after free).
  // |underlying_task_runner_| being ref-counted and potentially kept alive by
  // many SingleThreadTaskRunner refs, the best we can do is tell it to shutdown
  // after which it will start returning false to PostTasks that happen-after
  // this point (note that invoking Shutdown() first would not remove the need
  // to DisconnectFromParent() since the controller is invoked *after* a task is
  // enqueued and the incoming queue's lock is released (see
  // MessageLoopTaskRunner::AddToIncomingQueue()).
  // Details : while an "in-progress post tasks" refcount in Controller in lieu
  // of |message_loop_lock_| would be an option to handle the "pending post
  // tasks on shutdown" case, |message_loop_lock_| would still be required to
  // serialize ScheduleWork() call and as such that optimization isn't worth it.
  message_loop_controller_->DisconnectFromParent();
  underlying_task_runner_->Shutdown();

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
  task_observers_.push_back(task_observer);
}

void MessageLoop::RemoveTaskObserver(TaskObserver* task_observer) {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  auto it =
      std::find(task_observers_.begin(), task_observers_.end(), task_observer);
  DCHECK(it != task_observers_.end());
  task_observers_.erase(it);
}

void MessageLoop::SetAddQueueTimeToTasks(bool enable) {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  underlying_task_runner_->SetAddQueueTimeToTasks(enable);
}

bool MessageLoop::IsIdleForTesting() {
  // Have unprocessed tasks? (this reloads the work queue if necessary)
  if (sequenced_task_source_->HasTasks())
    return false;

  // Have unprocessed deferred tasks which can be processed at this run-level?
  if (pending_task_queue_.deferred_tasks().HasTasks() &&
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
      message_loop_controller_(
          new Controller(this)),  // Ownership transferred on the next line.
      underlying_task_runner_(MakeRefCounted<internal::MessageLoopTaskRunner>(
          WrapUnique(message_loop_controller_))),
      sequenced_task_source_(underlying_task_runner_.get()),
      task_runner_(underlying_task_runner_) {
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

  underlying_task_runner_->BindToCurrentThread();
  message_loop_controller_->StartScheduling();
  SetThreadTaskRunnerHandle();
  thread_id_ = PlatformThread::CurrentId();

  scoped_set_sequence_local_storage_map_for_current_thread_ = std::make_unique<
      internal::ScopedSetSequenceLocalStorageMapForCurrentThread>(
      &sequence_local_storage_map_);

  RunLoop::RegisterDelegateForCurrentThread(this);

#if defined(OS_ANDROID)
  // On Android, attach to the native loop when there is one.
  if (type_ == TYPE_UI || type_ == TYPE_JAVA)
    static_cast<MessagePumpForUI*>(pump_.get())->Attach(this);
#endif
}

std::string MessageLoop::GetThreadName() const {
  DCHECK_NE(kInvalidThreadId, thread_id_)
      << "GetThreadName() must only be called after BindToCurrentThread()'s "
      << "side-effects have been synchronized with this thread.";
  return ThreadIdNameManager::GetInstance()->GetName(thread_id_);
}

void MessageLoop::SetTaskRunner(
    scoped_refptr<SingleThreadTaskRunner> task_runner) {
  DCHECK(task_runner);
  if (thread_id_ == kInvalidThreadId) {
    // ThreadTaskRunnerHandle will be set during BindToCurrentThread().
    task_runner_ = std::move(task_runner);
  } else {
    // Once MessageLoop is bound, |task_runner_| may only be altered on the
    // bound thread.
    DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
    DCHECK(task_runner->BelongsToCurrentThread());
    task_runner_ = std::move(task_runner);
    SetThreadTaskRunnerHandle();
  }
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
  if (sequenced_task_source_->HasTasks())
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

  while (pending_task_queue_.deferred_tasks().HasTasks()) {
    PendingTask pending_task = pending_task_queue_.deferred_tasks().Pop();
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

  for (TaskObserver* task_observer : task_observers_)
    task_observer->WillProcessTask(*pending_task);
  message_loop_controller_->task_annotator().RunTask("MessageLoop::PostTask",
                                                     pending_task);
  for (TaskObserver* task_observer : task_observers_)
    task_observer->DidProcessTask(*pending_task);

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
  pending_task_queue_.deferred_tasks().Push(std::move(pending_task));
  return false;
}

void MessageLoop::DeletePendingTasks() {
  // Delete all currently pending tasks but not tasks potentially posted from
  // their destructors. See ~MessageLoop() for the full logic mitigating against
  // infite loops when clearing pending tasks. The ScopedClosureRunner below
  // will be bound to a task posted at the end of the queue. After it is posted,
  // tasks will be deleted one by one, when the bound ScopedClosureRunner is
  // deleted and sets |deleted_all_originally_pending|, we know we've deleted
  // all originally pending tasks.
  bool deleted_all_originally_pending = false;
  ScopedClosureRunner capture_deleted_all_originally_pending(BindOnce(
      [](bool* deleted_all_originally_pending) {
        *deleted_all_originally_pending = true;
      },
      Unretained(&deleted_all_originally_pending)));
  sequenced_task_source_->InjectTask(
      BindOnce([](ScopedClosureRunner) {},
               std::move(capture_deleted_all_originally_pending)));

  while (!deleted_all_originally_pending) {
    PendingTask pending_task = sequenced_task_source_->TakeTask();

    // New delayed tasks should be deleted after older ones.
    if (!pending_task.delayed_run_time.is_null())
      pending_task_queue_.delayed_tasks().Push(std::move(pending_task));
  }

  pending_task_queue_.deferred_tasks().Clear();
  // TODO(robliao): Determine if we can move delayed task destruction before
  // deferred tasks to maintain the MessagePump DoWork, DoDelayedWork, and
  // DoIdleWork processing order.
  pending_task_queue_.delayed_tasks().Clear();
}

void MessageLoop::ScheduleWork() {
  pump_->ScheduleWork();
}

TimeTicks MessageLoop::CapAtOneDay(TimeTicks next_run_time) {
  return std::min(next_run_time, recent_time_ + TimeDelta::FromDays(1));
}

bool MessageLoop::DoWork() {
  if (!task_execution_allowed_)
    return false;

  // Execute oldest task.
  while (sequenced_task_source_->HasTasks()) {
    PendingTask pending_task = sequenced_task_source_->TakeTask();
    if (pending_task.task.IsCancelled())
      continue;

    if (!pending_task.delayed_run_time.is_null()) {
      int sequence_num = pending_task.sequence_num;
      TimeTicks delayed_run_time = pending_task.delayed_run_time;
      pending_task_queue_.delayed_tasks().Push(std::move(pending_task));
      // If we changed the topmost task, then it is time to reschedule.
      if (pending_task_queue_.delayed_tasks().Peek().sequence_num ==
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
      !pending_task_queue_.delayed_tasks().HasTasks()) {
    *next_delayed_work_time = TimeTicks();
    return false;
  }

  // When we "fall behind", there will be a lot of tasks in the delayed work
  // queue that are ready to run.  To increase efficiency when we fall behind,
  // we will only call Time::Now() intermittently, and then process all tasks
  // that are ready to run before calling it again.  As a result, the more we
  // fall behind (and have a lot of ready-to-run delayed tasks), the more
  // efficient we'll be at handling the tasks.

  TimeTicks next_run_time =
      pending_task_queue_.delayed_tasks().Peek().delayed_run_time;

  if (next_run_time > recent_time_) {
    recent_time_ = TimeTicks::Now();  // Get a better view of Now();
    if (next_run_time > recent_time_) {
      *next_delayed_work_time = CapAtOneDay(next_run_time);
      return false;
    }
  }

  PendingTask pending_task = pending_task_queue_.delayed_tasks().Pop();

  if (pending_task_queue_.delayed_tasks().HasTasks()) {
    *next_delayed_work_time = CapAtOneDay(
        pending_task_queue_.delayed_tasks().Peek().delayed_run_time);
  }

  return DeferOrRunPendingTask(std::move(pending_task));
}

bool MessageLoop::DoIdleWork() {
  if (ProcessNextDelayedNonNestableTask())
    return true;

#if defined(OS_WIN)
  bool need_high_res_timers = false;
#endif

  // Do not report idle metrics if about to quit the loop and/or in a nested
  // loop where |!task_execution_allowed_|. In the former case, the loop isn't
  // going to sleep and in the latter case DoDelayedWork() will not actually do
  // the work this is prepping for.
  if (ShouldQuitWhenIdle()) {
    pump_->Quit();
  } else if (task_execution_allowed_) {
    // Only track idle metrics in MessageLoopForUI to avoid too much contention
    // logging the histogram (https://crbug.com/860801) -- there's typically
    // only one UI thread per process and, for practical purposes, restricting
    // the MessageLoop diagnostic metrics to it yields similar information.
    if (type_ == TYPE_UI)
      pending_task_queue_.ReportMetricsOnIdle();

#if defined(OS_WIN)
    // On Windows we activate the high resolution timer so that the wait
    // _if_ triggered by the timer happens with good resolution. If we don't
    // do this the default resolution is 15ms which might not be acceptable
    // for some tasks.
    need_high_res_timers = pending_task_queue_.HasPendingHighResolutionTasks();
#endif
  }

#if defined(OS_WIN)
  if (in_high_res_mode_ != need_high_res_timers) {
    in_high_res_mode_ = need_high_res_timers;
    Time::ActivateHighResolutionTimer(in_high_res_mode_);
  }
#endif

  // When we return we will do a kernel wait for more tasks.
  return false;
}

#if !defined(OS_NACL)

//------------------------------------------------------------------------------
// MessageLoopForUI

MessageLoopForUI::MessageLoopForUI(Type type) : MessageLoop(type) {
#if defined(OS_ANDROID)
  DCHECK(type == TYPE_UI || type == TYPE_JAVA);
#else
  DCHECK_EQ(type, TYPE_UI);
#endif
}

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
void MessageLoopForUI::Abort() {
  static_cast<MessagePumpForUI*>(pump_.get())->Abort();
}

bool MessageLoopForUI::IsAborted() {
  return static_cast<MessagePumpForUI*>(pump_.get())->IsAborted();
}

void MessageLoopForUI::QuitWhenIdle(base::OnceClosure callback) {
  static_cast<MessagePumpForUI*>(pump_.get())
      ->QuitWhenIdle(std::move(callback));
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
