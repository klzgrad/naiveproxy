// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_ASYNC_DISPATCHER_H_
#define BASE_FUCHSIA_ASYNC_DISPATCHER_H_

#include <lib/async/dispatcher.h>
#include <lib/async/exception.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/timer.h>

#include "base/containers/linked_list.h"
#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"

namespace base {

// Implementation of dispatcher for Fuchsia's async library. It's necessary to
// run Fuchsia's library on chromium threads.
class BASE_EXPORT AsyncDispatcher : public async_dispatcher_t {
 public:
  AsyncDispatcher();
  ~AsyncDispatcher();

  // Returns after running one or more tasks or waits until |deadline|.
  // Returns |ZX_OK| if some tasks were executed, |ZX_ERR_TIMED_OUT| - the
  // deadline expired, |ZX_ERR_CANCELED| - Stop() was called.
  zx_status_t DispatchOrWaitUntil(zx_time_t deadline);

  // If Run() is being executed then it will return as soon as possible (e.g.
  // finishing running the current task), otherwise the following Run() call
  // will quit immediately instead of waiting until deadline expires.
  void Stop();

 private:
  class ExceptionState;
  class WaitState;
  class TaskState;

  // ASYNC_OPS_V1 operations.
  static zx_time_t NowOp(async_dispatcher_t* async);
  static zx_status_t BeginWaitOp(async_dispatcher_t* async, async_wait_t* wait);
  static zx_status_t CancelWaitOp(async_dispatcher_t* async,
                                  async_wait_t* wait);
  static zx_status_t PostTaskOp(async_dispatcher_t* async, async_task_t* task);
  static zx_status_t CancelTaskOp(async_dispatcher_t* async,
                                  async_task_t* task);
  static zx_status_t QueuePacketOp(async_dispatcher_t* async,
                                   async_receiver_t* receiver,
                                   const zx_packet_user_t* data);
  static zx_status_t SetGuestBellTrapOp(async_dispatcher_t* async,
                                        async_guest_bell_trap_t* trap,
                                        zx_handle_t guest,
                                        zx_vaddr_t addr,
                                        size_t length);

  // ASYNC_OPS_V2 operations.
  static zx_status_t BindExceptionPortOp(async_dispatcher_t* dispatcher,
                                         async_exception_t* exception);
  static zx_status_t UnbindExceptionPortOp(async_dispatcher_t* dispatcher,
                                           async_exception_t* exception);

  // async_ops_t implementation. Called by corresponding *Op() methods above.
  zx_status_t BeginWait(async_wait_t* wait);
  zx_status_t CancelWait(async_wait_t* wait);
  zx_status_t PostTask(async_task_t* task);
  zx_status_t CancelTask(async_task_t* task);
  zx_status_t BindExceptionPort(async_exception_t* exception);
  zx_status_t UnbindExceptionPort(async_exception_t* exception);

  // Runs tasks in |task_list_| that have deadline in the past.
  void DispatchTasks();

  // Must be called while |lock_| is held.
  void RestartTimerLocked();

  THREAD_CHECKER(thread_checker_);

  zx::port port_;
  zx::timer timer_;
  zx::event stop_event_;

  LinkedList<WaitState> wait_list_;
  LinkedList<ExceptionState> exception_list_;

  async_ops_t ops_storage_;

  // |lock_| must be held when accessing |task_list_|.
  base::Lock lock_;

  LinkedList<TaskState> task_list_;

  DISALLOW_COPY_AND_ASSIGN(AsyncDispatcher);
};

}  // namespace base

#endif  // BASE_FUCHSIA_ASYNC_DISPATCHER_H_
