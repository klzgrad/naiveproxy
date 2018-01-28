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
#include "base/run_loop.h"
#include "base/third_party/dynamic_annotations/dynamic_annotations.h"
#include "base/threading/thread_id_name_manager.h"
#include "base/threading/thread_local.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"

#if defined(OS_MACOSX)
#include "base/message_loop/message_pump_mac.h"
#endif
#if defined(OS_POSIX) && !defined(OS_IOS) && !defined(OS_FUCHSIA)
#include "base/message_loop/message_pump_libevent.h"
#endif
#if defined(OS_FUCHSIA)
#include "base/message_loop/message_pump_fuchsia.h"
#endif
#if defined(OS_ANDROID)
#include "base/message_loop/message_pump_android.h"
#endif
#if defined(USE_GLIB)
#include "base/message_loop/message_pump_glib.h"
#endif

namespace base {

namespace {

// A lazily created thread local storage for quick access to a thread's message
// loop, if one exists.
base::ThreadLocalPointer<MessageLoop>* GetTLSMessageLoop() {
  static auto* lazy_tls_ptr = new base::ThreadLocalPointer<MessageLoop>();
  return lazy_tls_ptr;
}
MessageLoop::MessagePumpFactory* message_pump_for_ui_factory_ = nullptr;

#if defined(OS_IOS)
using MessagePumpForIO = MessagePumpIOSForIO;
#elif defined(OS_NACL_SFI)
using MessagePumpForIO = MessagePumpDefault;
#elif defined(OS_FUCHSIA)
using MessagePumpForIO = MessagePumpFuchsia;
#elif defined(OS_POSIX)
using MessagePumpForIO = MessagePumpLibevent;
#endif

#if !defined(OS_NACL_SFI)
MessagePumpForIO* ToPumpIO(MessagePump* pump) {
  return static_cast<MessagePumpForIO*>(pump);
}
#endif  // !defined(OS_NACL_SFI)

std::unique_ptr<MessagePump> ReturnPump(std::unique_ptr<MessagePump> pump) {
  return pump;
}

}  // namespace

//------------------------------------------------------------------------------

MessageLoop::TaskObserver::TaskObserver() = default;

MessageLoop::TaskObserver::~TaskObserver() = default;

MessageLoop::DestructionObserver::~DestructionObserver() = default;

//------------------------------------------------------------------------------

MessageLoop::MessageLoop(Type type)
    : MessageLoop(type, MessagePumpFactoryCallback()) {
  BindToCurrentThread();
}

MessageLoop::MessageLoop(std::unique_ptr<MessagePump> pump)
    : MessageLoop(TYPE_CUSTOM, BindOnce(&ReturnPump, Passed(&pump))) {
  BindToCurrentThread();
}

MessageLoop::~MessageLoop() {
  // If |pump_| is non-null, this message loop has been bound and should be the
  // current one on this thread. Otherwise, this loop is being destructed before
  // it was bound to a thread, so a different message loop (or no loop at all)
  // may be current.
  DCHECK((pump_ && current() == this) || (!pump_ && current() != this));

  // iOS just attaches to the loop, it doesn't Run it.
  // TODO(stuartmorgan): Consider wiring up a Detach().
#if !defined(OS_IOS)
  // There should be no active RunLoops on this thread, unless this MessageLoop
  // isn't bound to the current thread (see other condition at the top of this
  // method).
  DCHECK((!pump_ && current() != this) || !RunLoop::IsRunningOnCurrentThread());
#endif

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
  if (current() == this)
    GetTLSMessageLoop()->Set(nullptr);
}

// static
MessageLoop* MessageLoop::current() {
  // TODO(darin): sadly, we cannot enable this yet since people call us even
  // when they have no intention of using us.
  // DCHECK(loop) << "Ouch, did you forget to initialize me?";
  return GetTLSMessageLoop()->Get();
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
// TODO(rvargas): Get rid of the OS guards.
#if defined(USE_GLIB) && !defined(OS_NACL)
  using MessagePumpForUI = MessagePumpGlib;
#elif (defined(OS_LINUX) && !defined(OS_NACL)) || defined(OS_BSD)
  using MessagePumpForUI = MessagePumpLibevent;
#elif defined(OS_FUCHSIA)
  using MessagePumpForUI = MessagePumpFuchsia;
#endif

#if defined(OS_IOS) || defined(OS_MACOSX)
#define MESSAGE_PUMP_UI std::unique_ptr<MessagePump>(MessagePumpMac::Create())
#elif defined(OS_NACL) || defined(OS_AIX)
// Currently NaCl and AIX don't have a UI MessageLoop.
// TODO(abarth): Figure out if we need this.
#define MESSAGE_PUMP_UI std::unique_ptr<MessagePump>()
#else
#define MESSAGE_PUMP_UI std::unique_ptr<MessagePump>(new MessagePumpForUI())
#endif

#if defined(OS_MACOSX)
  // Use an OS native runloop on Mac to support timer coalescing.
#define MESSAGE_PUMP_DEFAULT \
  std::unique_ptr<MessagePump>(new MessagePumpCFRunLoop())
#else
#define MESSAGE_PUMP_DEFAULT \
  std::unique_ptr<MessagePump>(new MessagePumpDefault())
#endif

  if (type == MessageLoop::TYPE_UI) {
    if (message_pump_for_ui_factory_)
      return message_pump_for_ui_factory_();
    return MESSAGE_PUMP_UI;
  }
  if (type == MessageLoop::TYPE_IO)
    return std::unique_ptr<MessagePump>(new MessagePumpForIO());

#if defined(OS_ANDROID)
  if (type == MessageLoop::TYPE_JAVA)
    return std::unique_ptr<MessagePump>(new MessagePumpForUI());
#endif

  DCHECK_EQ(MessageLoop::TYPE_DEFAULT, type);
  return MESSAGE_PUMP_DEFAULT;
}

void MessageLoop::AddDestructionObserver(
    DestructionObserver* destruction_observer) {
  DCHECK_EQ(this, current());
  destruction_observers_.AddObserver(destruction_observer);
}

void MessageLoop::RemoveDestructionObserver(
    DestructionObserver* destruction_observer) {
  DCHECK_EQ(this, current());
  destruction_observers_.RemoveObserver(destruction_observer);
}

bool MessageLoop::IsType(Type type) const {
  return type_ == type;
}

// static
Closure MessageLoop::QuitWhenIdleClosure() {
  return Bind(&RunLoop::QuitCurrentWhenIdleDeprecated);
}

void MessageLoop::SetNestableTasksAllowed(bool allowed) {
  if (allowed) {
    CHECK(RunLoop::IsNestingAllowedOnCurrentThread());

    // Kick the native pump just in case we enter a OS-driven nested message
    // loop that does not go through RunLoop::Run().
    pump_->ScheduleWork();
  }
  task_execution_allowed_ = allowed;
}

bool MessageLoop::NestableTasksAllowed() const {
  return task_execution_allowed_;
}

// TODO(gab): Migrate TaskObservers to RunLoop as part of separating concerns
// between MessageLoop and RunLoop and making MessageLoop a swappable
// implementation detail. http://crbug.com/703346
void MessageLoop::AddTaskObserver(TaskObserver* task_observer) {
  DCHECK_EQ(this, current());
  CHECK(allow_task_observers_);
  task_observers_.AddObserver(task_observer);
}

void MessageLoop::RemoveTaskObserver(TaskObserver* task_observer) {
  DCHECK_EQ(this, current());
  CHECK(allow_task_observers_);
  task_observers_.RemoveObserver(task_observer);
}

bool MessageLoop::IsIdleForTesting() {
  // We only check the incoming queue, since we don't want to lock the work
  // queue.
  return incoming_task_queue_->IsIdleForTesting();
}

//------------------------------------------------------------------------------

// static
std::unique_ptr<MessageLoop> MessageLoop::CreateUnbound(
    Type type,
    MessagePumpFactoryCallback pump_factory) {
  return WrapUnique(new MessageLoop(type, std::move(pump_factory)));
}

MessageLoop::MessageLoop(Type type, MessagePumpFactoryCallback pump_factory)
    : type_(type),
      pump_factory_(std::move(pump_factory)),
      incoming_task_queue_(new internal::IncomingTaskQueue(this)),
      unbound_task_runner_(
          new internal::MessageLoopTaskRunner(incoming_task_queue_)),
      task_runner_(unbound_task_runner_) {
  // If type is TYPE_CUSTOM non-null pump_factory must be given.
  DCHECK(type_ != TYPE_CUSTOM || !pump_factory_.is_null());
}

void MessageLoop::BindToCurrentThread() {
  DCHECK(!pump_);
  if (!pump_factory_.is_null())
    pump_ = std::move(pump_factory_).Run();
  else
    pump_ = CreateMessagePumpForType(type_);

  DCHECK(!current()) << "should only have one message loop per thread";
  GetTLSMessageLoop()->Set(this);

  incoming_task_queue_->StartScheduling();
  unbound_task_runner_->BindToCurrentThread();
  unbound_task_runner_ = nullptr;
  SetThreadTaskRunnerHandle();
  thread_id_ = PlatformThread::CurrentId();

  scoped_set_sequence_local_storage_map_for_current_thread_ = std::make_unique<
      internal::ScopedSetSequenceLocalStorageMapForCurrentThread>(
      &sequence_local_storage_map_);

  run_loop_client_ = RunLoop::RegisterDelegateForCurrentThread(this);
}

std::string MessageLoop::GetThreadName() const {
  DCHECK_NE(kInvalidThreadId, thread_id_)
      << "GetThreadName() must only be called after BindToCurrentThread()'s "
      << "side-effects have been synchronized with this thread.";
  return ThreadIdNameManager::GetInstance()->GetName(thread_id_);
}

void MessageLoop::SetTaskRunner(
    scoped_refptr<SingleThreadTaskRunner> task_runner) {
  DCHECK_EQ(this, current());
  DCHECK(task_runner);
  DCHECK(task_runner->BelongsToCurrentThread());
  DCHECK(!unbound_task_runner_);
  task_runner_ = std::move(task_runner);
  SetThreadTaskRunnerHandle();
}

void MessageLoop::ClearTaskRunnerForTesting() {
  DCHECK_EQ(this, current());
  DCHECK(!unbound_task_runner_);
  task_runner_ = nullptr;
  thread_task_runner_handle_.reset();
}

void MessageLoop::Run(bool application_tasks_allowed) {
  DCHECK_EQ(this, current());
  if (application_tasks_allowed && !task_execution_allowed_) {
    // Allow nested task execution as explicitly requested.
    DCHECK(run_loop_client_->IsNested());
    task_execution_allowed_ = true;
    pump_->Run(this);
    task_execution_allowed_ = false;
  } else {
    pump_->Run(this);
  }
}

void MessageLoop::Quit() {
  DCHECK_EQ(this, current());
  pump_->Quit();
}

void MessageLoop::EnsureWorkScheduled() {
  DCHECK_EQ(this, current());
  if (incoming_task_queue_->triage_tasks().HasTasks())
    pump_->ScheduleWork();
}

void MessageLoop::SetThreadTaskRunnerHandle() {
  DCHECK_EQ(this, current());
  // Clear the previous thread task runner first, because only one can exist at
  // a time.
  thread_task_runner_handle_.reset();
  thread_task_runner_handle_.reset(new ThreadTaskRunnerHandle(task_runner_));
}

bool MessageLoop::ProcessNextDelayedNonNestableTask() {
  if (run_loop_client_->IsNested())
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
  current_pending_task_ = pending_task;

  // Execute the task and assume the worst: It is probably not reentrant.
  task_execution_allowed_ = false;

  TRACE_TASK_EXECUTION("MessageLoop::RunTask", *pending_task);

  for (auto& observer : task_observers_)
    observer.WillProcessTask(*pending_task);
  incoming_task_queue_->RunTask(pending_task);
  for (auto& observer : task_observers_)
    observer.DidProcessTask(*pending_task);

  task_execution_allowed_ = true;

  current_pending_task_ = nullptr;
}

bool MessageLoop::DeferOrRunPendingTask(PendingTask pending_task) {
  if (pending_task.nestable == Nestable::kNestable ||
      !run_loop_client_->IsNested()) {
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

  if (run_loop_client_->ShouldQuitWhenIdle())
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

#if defined(OS_ANDROID)
void MessageLoopForUI::Start() {
  // No Histogram support for UI message loop as it is managed by Java side
  static_cast<MessagePumpForUI*>(pump_.get())->Start(this);
}

void MessageLoopForUI::Abort() {
  static_cast<MessagePumpForUI*>(pump_.get())->Abort();
}
#endif

#if defined(OS_IOS)
void MessageLoopForUI::Attach() {
  static_cast<MessagePumpUIApplication*>(pump_.get())->Attach(this);
}
#endif

#if (defined(USE_OZONE) && !defined(OS_FUCHSIA)) || \
    (defined(USE_X11) && !defined(USE_GLIB))
bool MessageLoopForUI::WatchFileDescriptor(
    int fd,
    bool persistent,
    MessagePumpLibevent::Mode mode,
    MessagePumpLibevent::FileDescriptorWatcher *controller,
    MessagePumpLibevent::Watcher *delegate) {
  return static_cast<MessagePumpLibevent*>(pump_.get())->WatchFileDescriptor(
      fd,
      persistent,
      mode,
      controller,
      delegate);
}
#endif

#endif  // !defined(OS_NACL)

//------------------------------------------------------------------------------
// MessageLoopForIO

#if !defined(OS_NACL_SFI)

#if defined(OS_WIN)
void MessageLoopForIO::RegisterIOHandler(HANDLE file, IOHandler* handler) {
  ToPumpIO(pump_.get())->RegisterIOHandler(file, handler);
}

bool MessageLoopForIO::RegisterJobObject(HANDLE job, IOHandler* handler) {
  return ToPumpIO(pump_.get())->RegisterJobObject(job, handler);
}

bool MessageLoopForIO::WaitForIOCompletion(DWORD timeout, IOHandler* filter) {
  return ToPumpIO(pump_.get())->WaitForIOCompletion(timeout, filter);
}
#elif defined(OS_POSIX)
bool MessageLoopForIO::WatchFileDescriptor(int fd,
                                           bool persistent,
                                           Mode mode,
                                           FileDescriptorWatcher* controller,
                                           Watcher* delegate) {
  return ToPumpIO(pump_.get())->WatchFileDescriptor(
      fd,
      persistent,
      mode,
      controller,
      delegate);
}
#endif

#endif  // !defined(OS_NACL_SFI)

#if defined(OS_FUCHSIA)
// Additional watch API for native platform resources.
bool MessageLoopForIO::WatchZxHandle(zx_handle_t handle,
                                     bool persistent,
                                     zx_signals_t signals,
                                     ZxHandleWatchController* controller,
                                     ZxHandleWatcher* delegate) {
  return ToPumpIO(pump_.get())
      ->WatchZxHandle(handle, persistent, signals, controller, delegate);
}
#endif

}  // namespace base
