// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop.h"

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_pump_default.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/message_loop/message_pump_for_ui.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/task/sequence_manager/sequence_manager.h"
#include "base/task/sequence_manager/sequence_manager_impl.h"
#include "base/task/sequence_manager/task_queue.h"
#include "build/build_config.h"

#if defined(OS_MACOSX)
#include "base/message_loop/message_pump_mac.h"
#endif

namespace base {

namespace {

MessageLoop::MessagePumpFactory* message_pump_for_ui_factory_ = nullptr;

}  // namespace

// Unfortunately since we're not on C++17 we're required to provide an out of
// line definition.
constexpr MessageLoop::Type MessageLoop::TYPE_DEFAULT;
constexpr MessageLoop::Type MessageLoop::TYPE_UI;
constexpr MessageLoop::Type MessageLoop::TYPE_CUSTOM;
constexpr MessageLoop::Type MessageLoop::TYPE_IO;
#if defined(OS_ANDROID)
constexpr MessageLoop::Type MessageLoop::TYPE_JAVA;
#endif

MessageLoop::MessageLoop(Type type) : MessageLoop(type, nullptr) {
  // For TYPE_CUSTOM you must either use
  // MessageLoop(std::unique_ptr<MessagePump> pump) or
  // MessageLoop::CreateUnbound()
  DCHECK_NE(type_, TYPE_CUSTOM);
  BindToCurrentThread();
}

MessageLoop::MessageLoop(std::unique_ptr<MessagePump> pump)
    : MessageLoop(TYPE_CUSTOM, std::move(pump)) {
  BindToCurrentThread();
}

MessageLoop::~MessageLoop() {
  // Clean up any unprocessed tasks, but take care: deleting a task could
  // result in the addition of more tasks (e.g., via DeleteSoon).  We set a
  // limit on the number of times we will allow a deleted task to generate more
  // tasks.  Normally, we should only pass through this loop once or twice.  If
  // we end up hitting the loop limit, then it is probably due to one task that
  // is being stubborn.  Inspect the queues to see who is left.
  bool tasks_remain;
  for (int i = 0; i < 100; ++i) {
    backend_->DeletePendingTasks();
    // If we end up with empty queues, then break out of the loop.
    tasks_remain = backend_->HasTasks();
    if (!tasks_remain)
      break;
  }
  DCHECK(!tasks_remain);

  // If |pump_| is non-null, this message loop has been bound and should be the
  // current one on this thread. Otherwise, this loop is being destructed before
  // it was bound to a thread, so a different message loop (or no loop at all)
  // may be current.
  DCHECK((pump_ && IsBoundToCurrentThread()) ||
         (!pump_ && !IsBoundToCurrentThread()));

// iOS just attaches to the loop, it doesn't Run it.
// TODO(stuartmorgan): Consider wiring up a Detach().
#if !defined(OS_IOS)
  // There should be no active RunLoops on this thread, unless this MessageLoop
  // isn't bound to the current thread (see other condition at the top of this
  // method).
  DCHECK((!pump_ && !IsBoundToCurrentThread()) ||
         !RunLoop::IsRunningOnCurrentThread());
#endif  // !defined(OS_IOS)
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
    return std::make_unique<MessagePumpForIO>();

#if defined(OS_ANDROID)
  if (type == MessageLoop::TYPE_JAVA)
    return std::make_unique<MessagePumpForUI>();
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
  backend_->AddTaskObserver(task_observer);
}

void MessageLoop::RemoveTaskObserver(TaskObserver* task_observer) {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  backend_->RemoveTaskObserver(task_observer);
}

bool MessageLoop::IsBoundToCurrentThread() const {
  return backend_->IsBoundToCurrentThread();
}

bool MessageLoop::IsIdleForTesting() {
  return backend_->IsIdleForTesting();
}

MessageLoopBase* MessageLoop::GetMessageLoopBase() {
  return backend_.get();
}

//------------------------------------------------------------------------------

// static
std::unique_ptr<MessageLoop> MessageLoop::CreateUnbound(Type type) {
  return WrapUnique(new MessageLoop(type, nullptr));
}

// static
std::unique_ptr<MessageLoop> MessageLoop::CreateUnbound(
    std::unique_ptr<MessagePump> custom_pump) {
  return WrapUnique(new MessageLoop(TYPE_CUSTOM, std::move(custom_pump)));
}

MessageLoop::MessageLoop(Type type, std::unique_ptr<MessagePump> custom_pump)
    : backend_(sequence_manager::internal::SequenceManagerImpl::CreateUnbound(
          sequence_manager::SequenceManager::Settings{.message_loop_type =
                                                          type})),
      default_task_queue_(CreateDefaultTaskQueue()),
      type_(type),
      custom_pump_(std::move(custom_pump)) {
  // Bound in BindToCurrentThread();
  DETACH_FROM_THREAD(bound_thread_checker_);
}

scoped_refptr<sequence_manager::TaskQueue>
MessageLoop::CreateDefaultTaskQueue() {
  sequence_manager::internal::SequenceManagerImpl* manager =
      static_cast<sequence_manager::internal::SequenceManagerImpl*>(
          backend_.get());
  scoped_refptr<sequence_manager::TaskQueue> default_task_queue =
      manager->CreateTaskQueueWithType<sequence_manager::TaskQueue>(
          sequence_manager::TaskQueue::Spec("default_tq"));
  manager->SetTaskRunner(default_task_queue->task_runner());
  return default_task_queue;
}

void MessageLoop::BindToCurrentThread() {
  DCHECK_CALLED_ON_VALID_THREAD(bound_thread_checker_);
  thread_id_ = PlatformThread::CurrentId();

  DCHECK(!pump_);

  std::unique_ptr<MessagePump> pump = CreateMessagePump();
  pump_ = pump.get();

  DCHECK(!MessageLoopCurrent::IsSet())
      << "should only have one message loop per thread";

  backend_->BindToCurrentThread(std::move(pump));
}

std::unique_ptr<MessagePump> MessageLoop::CreateMessagePump() {
  if (custom_pump_) {
    return std::move(custom_pump_);
  } else {
    return CreateMessagePumpForType(type_);
  }
}

void MessageLoop::SetTimerSlack(TimerSlack timer_slack) {
  backend_->SetTimerSlack(timer_slack);
}

std::string MessageLoop::GetThreadName() const {
  return backend_->GetThreadName();
}

scoped_refptr<SingleThreadTaskRunner> MessageLoop::task_runner() const {
  return backend_->GetTaskRunner();
}

void MessageLoop::SetTaskRunner(
    scoped_refptr<SingleThreadTaskRunner> task_runner) {
  DCHECK(task_runner);
  backend_->SetTaskRunner(task_runner);
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

#if defined(OS_IOS)
void MessageLoopForUI::Attach() {
  backend_->AttachToMessagePump();
}
#endif  // defined(OS_IOS)

#if defined(OS_ANDROID)
void MessageLoopForUI::Abort() {
  static_cast<MessagePumpForUI*>(pump_)->Abort();
}

bool MessageLoopForUI::IsAborted() {
  return static_cast<MessagePumpForUI*>(pump_)->IsAborted();
}

void MessageLoopForUI::QuitWhenIdle(base::OnceClosure callback) {
  static_cast<MessagePumpForUI*>(pump_)->QuitWhenIdle(std::move(callback));
}
#endif  // defined(OS_ANDROID)

#if defined(OS_WIN)
void MessageLoopForUI::EnableWmQuit() {
  static_cast<MessagePumpForUI*>(pump_)->EnableWmQuit();
}
#endif  // defined(OS_WIN)

#endif  // !defined(OS_NACL)

}  // namespace base
