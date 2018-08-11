// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/async_dispatcher.h"

#include <lib/async/default.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <zircon/syscalls.h>

#include "base/fuchsia/fuchsia_logging.h"

namespace base {

namespace {

template <typename T>
uintptr_t key_from_ptr(T* ptr) {
  return reinterpret_cast<uintptr_t>(ptr);
};

}  // namespace

class AsyncDispatcher::WaitState : public LinkNode<WaitState> {
 public:
  explicit WaitState(AsyncDispatcher* async_dispatcher) {
    async_dispatcher->wait_list_.Append(this);
  }
  ~WaitState() { RemoveFromList(); }

  async_wait_t* wait() {
    // WaitState objects are allocated in-place in the |state| field of an
    // enclosing async_wait_t, so async_wait_t address can be calculated by
    // subtracting state offset in async_wait_t from |this|.
    static_assert(std::is_standard_layout<async_wait_t>(),
                  "async_wait_t is expected to have standard layout.");
    return reinterpret_cast<async_wait_t*>(reinterpret_cast<uint8_t*>(this) -
                                           offsetof(async_wait_t, state));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(WaitState);
};

class AsyncDispatcher::TaskState : public LinkNode<TaskState> {
 public:
  explicit TaskState(LinkNode<TaskState>* previous_task) {
    InsertAfter(previous_task);
  }
  ~TaskState() { RemoveFromList(); }

  async_task_t* task() {
    // TaskState objects are allocated in-place in the |state| field of an
    // enclosing async_task_t, so async_task_t address can be calculated by
    // subtracting state offset in async_task_t from |this|.
    static_assert(std::is_standard_layout<async_task_t>(),
                  "async_task_t is expected to have standard layout.");
    return reinterpret_cast<async_task_t*>(reinterpret_cast<uint8_t*>(this) -
                                           offsetof(async_task_t, state));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TaskState);
};

AsyncDispatcher::AsyncDispatcher() : ops_storage_({}) {
  zx_status_t status = zx_port_create(0u, port_.receive());
  ZX_DCHECK(status == ZX_OK, status);

  status = zx_timer_create(0u, ZX_CLOCK_MONOTONIC, timer_.receive());
  ZX_DCHECK(status == ZX_OK, status);
  status =
      zx_object_wait_async(timer_.get(), port_.get(), key_from_ptr(&timer_),
                           ZX_TIMER_SIGNALED, ZX_WAIT_ASYNC_REPEATING);
  ZX_DCHECK(status == ZX_OK, status);

  status = zx_event_create(0, stop_event_.receive());
  ZX_DCHECK(status == ZX_OK, status);
  status = zx_object_wait_async(stop_event_.get(), port_.get(),
                                key_from_ptr(&stop_event_), ZX_EVENT_SIGNALED,
                                ZX_WAIT_ASYNC_REPEATING);
  ZX_DCHECK(status == ZX_OK, status);

  ops_storage_.v1.now = NowOp;
  ops_storage_.v1.begin_wait = BeginWaitOp;
  ops_storage_.v1.cancel_wait = CancelWaitOp;
  ops_storage_.v1.post_task = PostTaskOp;
  ops_storage_.v1.cancel_task = CancelTaskOp;
  ops_storage_.v1.queue_packet = QueuePacketOp;
  ops_storage_.v1.set_guest_bell_trap = SetGuestBellTrapOp;
  ops = &ops_storage_;

  DCHECK(!async_get_default());
  async_set_default(this);
}

AsyncDispatcher::~AsyncDispatcher() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK_EQ(async_get_default(), this);

  // Some waits and tasks may be canceled while the dispatcher is being
  // destroyed, so pop-from-head until none remain.

  while (!wait_list_.empty()) {
    WaitState* state = wait_list_.head()->value();
    async_wait_t* wait = state->wait();
    state->~WaitState();
    wait->handler(this, wait, ZX_ERR_CANCELED, nullptr);
  }

  while (!task_list_.empty()) {
    TaskState* state = task_list_.head()->value();
    async_task_t* task = state->task();
    state->~TaskState();
    task->handler(this, task, ZX_ERR_CANCELED);
  }

  async_set_default(nullptr);
}

zx_status_t AsyncDispatcher::DispatchOrWaitUntil(zx_time_t deadline) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  zx_port_packet_t packet = {};
  zx_status_t status = zx_port_wait(port_.get(), deadline, &packet, 1);
  if (status != ZX_OK)
    return status;

  if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE ||
      packet.type == ZX_PKT_TYPE_SIGNAL_REP) {
    if (packet.key == key_from_ptr(&timer_)) {
      // |timer_| has expired.
      DCHECK(packet.signal.observed & ZX_TIMER_SIGNALED);
      DispatchTasks();
      return ZX_OK;
    } else if (packet.key == key_from_ptr(&stop_event_)) {
      // Stop() was called.
      DCHECK(packet.signal.observed & ZX_EVENT_SIGNALED);
      status = zx_object_signal(stop_event_.get(), ZX_EVENT_SIGNALED, 0);
      ZX_DCHECK(status == ZX_OK, status);
      return ZX_ERR_CANCELED;
    } else {
      DCHECK_EQ(packet.type, ZX_PKT_TYPE_SIGNAL_ONE);
      async_wait_t* wait = reinterpret_cast<async_wait_t*>(packet.key);

      // Clean the state before invoking the handler: it may destroy the wait.
      WaitState* state = reinterpret_cast<WaitState*>(&wait->state);
      state->~WaitState();

      wait->handler(this, wait, packet.status, &packet.signal);

      return ZX_OK;
    }
  }

  NOTREACHED();
  return ZX_ERR_INTERNAL;
}

void AsyncDispatcher::Stop() {
  // Can be called on any thread.
  zx_status_t status =
      zx_object_signal(stop_event_.get(), 0, ZX_EVENT_SIGNALED);
  ZX_DCHECK(status == ZX_OK, status);
}

zx_time_t AsyncDispatcher::NowOp(async_t* async) {
  DCHECK(async);
  return zx_clock_get(ZX_CLOCK_MONOTONIC);
}

zx_status_t AsyncDispatcher::BeginWaitOp(async_t* async, async_wait_t* wait) {
  return static_cast<AsyncDispatcher*>(async)->BeginWait(wait);
}

zx_status_t AsyncDispatcher::CancelWaitOp(async_t* async, async_wait_t* wait) {
  return static_cast<AsyncDispatcher*>(async)->CancelWait(wait);
}

zx_status_t AsyncDispatcher::PostTaskOp(async_t* async, async_task_t* task) {
  return static_cast<AsyncDispatcher*>(async)->PostTask(task);
}

zx_status_t AsyncDispatcher::CancelTaskOp(async_t* async, async_task_t* task) {
  return static_cast<AsyncDispatcher*>(async)->CancelTask(task);
}

zx_status_t AsyncDispatcher::QueuePacketOp(async_t* async,
                                           async_receiver_t* receiver,
                                           const zx_packet_user_t* data) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AsyncDispatcher::SetGuestBellTrapOp(async_t* async,
                                                async_guest_bell_trap_t* trap,
                                                zx_handle_t guest,
                                                zx_vaddr_t addr,
                                                size_t length) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AsyncDispatcher::BeginWait(async_wait_t* wait) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  static_assert(sizeof(AsyncDispatcher::WaitState) <= sizeof(async_state_t),
                "WaitState is too big");
  WaitState* state = new (&wait->state) WaitState(this);
  zx_status_t status = zx_object_wait_async(wait->object, port_.get(),
                                            reinterpret_cast<uintptr_t>(wait),
                                            wait->trigger, ZX_WAIT_ASYNC_ONCE);

  if (status != ZX_OK)
    state->~WaitState();

  return status;
}

zx_status_t AsyncDispatcher::CancelWait(async_wait_t* wait) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  zx_status_t status =
      zx_port_cancel(port_.get(), wait->object, (uintptr_t)wait);
  if (status == ZX_OK) {
    WaitState* state = reinterpret_cast<WaitState*>(&(wait->state));
    state->~WaitState();
  }

  return status;
}

zx_status_t AsyncDispatcher::PostTask(async_task_t* task) {
  // Can be called on any thread.
  AutoLock lock(lock_);

  // Find correct position for the new task in |task_list_| to keep the list
  // sorted by deadline. This implementation has O(N) complexity, but it's
  // acceptable - async task are not expected to be used frequently.
  // TODO(sergeyu): Consider using a more efficient data structure if tasks
  // performance becomes important.
  LinkNode<TaskState>* node;
  for (node = task_list_.head(); node != task_list_.end();
       node = node->previous()) {
    if (task->deadline >= node->value()->task()->deadline)
      break;
  }

  static_assert(sizeof(AsyncDispatcher::TaskState) <= sizeof(async_state_t),
                "TaskState is too big");

  // Will insert new task after |node|.
  new (&task->state) TaskState(node);

  if (reinterpret_cast<TaskState*>(&task->state) == task_list_.head()) {
    // Task inserted at head. Earliest deadline changed.
    RestartTimerLocked();
  }

  return ZX_OK;
}

zx_status_t AsyncDispatcher::CancelTask(async_task_t* task) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  AutoLock lock(lock_);

  if (!task->state.reserved[0])
    return ZX_ERR_NOT_FOUND;

  TaskState* state = reinterpret_cast<TaskState*>(&task->state);
  state->~TaskState();

  return ZX_OK;
}

void AsyncDispatcher::DispatchTasks() {
  // Snapshot now value to set implicit bound for the tasks that will run before
  // DispatchTasks() returns. This also helps to avoid calling zx_clock_get()
  // more than necessary.
  zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);

  while (true) {
    async_task_t* task;
    {
      AutoLock lock(lock_);
      if (task_list_.empty())
        break;

      TaskState* task_state = task_list_.head()->value();
      task = task_state->task();

      if (task->deadline > now) {
        RestartTimerLocked();
        break;
      }

      task_state->~TaskState();

      // ~TaskState() is expected to reset the state to 0. The destructor
      // removes the task from the |task_list_| and LinkNode::RemoveFromList()
      // sets both its fields to nullptr, which is equivalent to resetting the
      // state to 0.
      DCHECK_EQ(task->state.reserved[0], 0u);
    }

    // The handler is responsible for freeing the |task| or it may reuse it.
    task->handler(this, task, ZX_OK);
  }
}

void AsyncDispatcher::RestartTimerLocked() {
  lock_.AssertAcquired();

  if (task_list_.empty())
    return;
  zx_time_t deadline = task_list_.head()->value()->task()->deadline;
  zx_status_t status = zx_timer_set(timer_.get(), deadline, 0);
  ZX_DCHECK(status == ZX_OK, status);
}

}  // namespace base
