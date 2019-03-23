// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop_current.h"

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/message_loop/message_pump_for_ui.h"
#include "base/no_destructor.h"
#include "base/threading/thread_local.h"
#include "base/threading/thread_task_runner_handle.h"

namespace base {

namespace {

base::ThreadLocalPointer<MessageLoopBase>* GetTLSMessageLoop() {
  static NoDestructor<ThreadLocalPointer<MessageLoopBase>> lazy_tls_ptr;
  return lazy_tls_ptr.get();
}

}  // namespace

//------------------------------------------------------------------------------
// MessageLoopCurrent

// static
MessageLoopCurrent MessageLoopCurrent::Get() {
  return MessageLoopCurrent(GetTLSMessageLoop()->Get());
}

// static
MessageLoopCurrent MessageLoopCurrent::GetNull() {
  return MessageLoopCurrent(nullptr);
}

// static
bool MessageLoopCurrent::IsSet() {
  return !!GetTLSMessageLoop()->Get();
}

void MessageLoopCurrent::AddDestructionObserver(
    DestructionObserver* destruction_observer) {
  DCHECK(current_->IsBoundToCurrentThread());
  current_->AddDestructionObserver(destruction_observer);
}

void MessageLoopCurrent::RemoveDestructionObserver(
    DestructionObserver* destruction_observer) {
  DCHECK(current_->IsBoundToCurrentThread());
  current_->RemoveDestructionObserver(destruction_observer);
}

std::string MessageLoopCurrent::GetThreadName() const {
  return current_->GetThreadName();
}

scoped_refptr<SingleThreadTaskRunner> MessageLoopCurrent::task_runner() const {
  DCHECK(current_->IsBoundToCurrentThread());
  return current_->GetTaskRunner();
}

void MessageLoopCurrent::SetTaskRunner(
    scoped_refptr<SingleThreadTaskRunner> task_runner) {
  DCHECK(current_->IsBoundToCurrentThread());
  current_->SetTaskRunner(std::move(task_runner));
}

bool MessageLoopCurrent::IsBoundToCurrentThread() const {
  return current_ == GetTLSMessageLoop()->Get();
}

bool MessageLoopCurrent::IsIdleForTesting() {
  DCHECK(current_->IsBoundToCurrentThread());
  return current_->IsIdleForTesting();
}

void MessageLoopCurrent::AddTaskObserver(TaskObserver* task_observer) {
  DCHECK(current_->IsBoundToCurrentThread());
  current_->AddTaskObserver(task_observer);
}

void MessageLoopCurrent::RemoveTaskObserver(TaskObserver* task_observer) {
  DCHECK(current_->IsBoundToCurrentThread());
  current_->RemoveTaskObserver(task_observer);
}

void MessageLoopCurrent::SetAddQueueTimeToTasks(bool enable) {
  DCHECK(current_->IsBoundToCurrentThread());
  current_->SetAddQueueTimeToTasks(enable);
}

void MessageLoopCurrent::SetNestableTasksAllowed(bool allowed) {
  DCHECK(current_->IsBoundToCurrentThread());
  current_->SetTaskExecutionAllowed(allowed);
}

bool MessageLoopCurrent::NestableTasksAllowed() const {
  return current_->IsTaskExecutionAllowed();
}

MessageLoopCurrent::ScopedNestableTaskAllower::ScopedNestableTaskAllower()
    : loop_(GetTLSMessageLoop()->Get()),
      old_state_(loop_->IsTaskExecutionAllowed()) {
  loop_->SetTaskExecutionAllowed(true);
}

MessageLoopCurrent::ScopedNestableTaskAllower::~ScopedNestableTaskAllower() {
  loop_->SetTaskExecutionAllowed(old_state_);
}

// static
void MessageLoopCurrent::BindToCurrentThreadInternal(MessageLoopBase* current) {
  DCHECK(!GetTLSMessageLoop()->Get())
      << "Can't register a second MessageLoop on the same thread.";
  GetTLSMessageLoop()->Set(current);
}

// static
void MessageLoopCurrent::UnbindFromCurrentThreadInternal(
    MessageLoopBase* current) {
  DCHECK_EQ(current, GetTLSMessageLoop()->Get());
  GetTLSMessageLoop()->Set(nullptr);
}

bool MessageLoopCurrent::operator==(const MessageLoopCurrent& other) const {
  return current_ == other.current_;
}

#if !defined(OS_NACL)

//------------------------------------------------------------------------------
// MessageLoopCurrentForUI

// static
MessageLoopCurrentForUI MessageLoopCurrentForUI::Get() {
  MessageLoopBase* loop = GetTLSMessageLoop()->Get();
  DCHECK(loop);
#if defined(OS_ANDROID)
  DCHECK(loop->IsType(MessageLoop::TYPE_UI) ||
         loop->IsType(MessageLoop::TYPE_JAVA));
#else   // defined(OS_ANDROID)
  DCHECK(loop->IsType(MessageLoop::TYPE_UI));
#endif  // defined(OS_ANDROID)
  return MessageLoopCurrentForUI(loop);
}

// static
bool MessageLoopCurrentForUI::IsSet() {
  MessageLoopBase* loop = GetTLSMessageLoop()->Get();
  return loop &&
#if defined(OS_ANDROID)
         (loop->IsType(MessageLoop::TYPE_UI) ||
          loop->IsType(MessageLoop::TYPE_JAVA));
#else   // defined(OS_ANDROID)
         loop->IsType(MessageLoop::TYPE_UI);
#endif  // defined(OS_ANDROID)
}

MessagePumpForUI* MessageLoopCurrentForUI::GetMessagePumpForUI() const {
  return static_cast<MessagePumpForUI*>(current_->GetMessagePump());
}

#if defined(USE_OZONE) && !defined(OS_FUCHSIA) && !defined(OS_WIN)
bool MessageLoopCurrentForUI::WatchFileDescriptor(
    int fd,
    bool persistent,
    MessagePumpForUI::Mode mode,
    MessagePumpForUI::FdWatchController* controller,
    MessagePumpForUI::FdWatcher* delegate) {
  DCHECK(current_->IsBoundToCurrentThread());
  return GetMessagePumpForUI()->WatchFileDescriptor(fd, persistent, mode,
                                                    controller, delegate);
}
#endif

#if defined(OS_IOS) || defined(OS_ANDROID)
void MessageLoopCurrentForUI::Attach() {
  current_->AttachToMessagePump();
}
#endif  // defined(OS_IOS)

#if defined(OS_ANDROID)
void MessageLoopCurrentForUI::Abort() {
  GetMessagePumpForUI()->Abort();
}
#endif  // defined(OS_ANDROID)

#if defined(OS_WIN)
void MessageLoopCurrentForUI::AddMessagePumpObserver(
    MessagePumpForUI::Observer* observer) {
  GetMessagePumpForUI()->AddObserver(observer);
}

void MessageLoopCurrentForUI::RemoveMessagePumpObserver(
    MessagePumpForUI::Observer* observer) {
  GetMessagePumpForUI()->RemoveObserver(observer);
}
#endif  // defined(OS_WIN)

#endif  // !defined(OS_NACL)

//------------------------------------------------------------------------------
// MessageLoopCurrentForIO

// static
MessageLoopCurrentForIO MessageLoopCurrentForIO::Get() {
  MessageLoopBase* loop = GetTLSMessageLoop()->Get();
  DCHECK(loop);
  DCHECK(loop->IsType(MessageLoop::TYPE_IO));
  return MessageLoopCurrentForIO(loop);
}

// static
bool MessageLoopCurrentForIO::IsSet() {
  MessageLoopBase* loop = GetTLSMessageLoop()->Get();
  return loop && loop->IsType(MessageLoop::TYPE_IO);
}

MessagePumpForIO* MessageLoopCurrentForIO::GetMessagePumpForIO() const {
  return static_cast<MessagePumpForIO*>(current_->GetMessagePump());
}

#if !defined(OS_NACL_SFI)

#if defined(OS_WIN)
HRESULT MessageLoopCurrentForIO::RegisterIOHandler(
    HANDLE file,
    MessagePumpForIO::IOHandler* handler) {
  DCHECK(current_->IsBoundToCurrentThread());
  return GetMessagePumpForIO()->RegisterIOHandler(file, handler);
}

bool MessageLoopCurrentForIO::RegisterJobObject(
    HANDLE job,
    MessagePumpForIO::IOHandler* handler) {
  DCHECK(current_->IsBoundToCurrentThread());
  return GetMessagePumpForIO()->RegisterJobObject(job, handler);
}

bool MessageLoopCurrentForIO::WaitForIOCompletion(
    DWORD timeout,
    MessagePumpForIO::IOHandler* filter) {
  DCHECK(current_->IsBoundToCurrentThread());
  return GetMessagePumpForIO()->WaitForIOCompletion(timeout, filter);
}
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
bool MessageLoopCurrentForIO::WatchFileDescriptor(
    int fd,
    bool persistent,
    MessagePumpForIO::Mode mode,
    MessagePumpForIO::FdWatchController* controller,
    MessagePumpForIO::FdWatcher* delegate) {
  DCHECK(current_->IsBoundToCurrentThread());
  return GetMessagePumpForIO()->WatchFileDescriptor(fd, persistent, mode,
                                                    controller, delegate);
}
#endif  // defined(OS_WIN)

#endif  // !defined(OS_NACL_SFI)

#if defined(OS_FUCHSIA)
// Additional watch API for native platform resources.
bool MessageLoopCurrentForIO::WatchZxHandle(
    zx_handle_t handle,
    bool persistent,
    zx_signals_t signals,
    MessagePumpForIO::ZxHandleWatchController* controller,
    MessagePumpForIO::ZxHandleWatcher* delegate) {
  DCHECK(current_->IsBoundToCurrentThread());
  return GetMessagePumpForIO()->WatchZxHandle(handle, persistent, signals,
                                              controller, delegate);
}
#endif

}  // namespace base
