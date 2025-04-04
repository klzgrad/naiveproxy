// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_pump_fuchsia.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/logging.h"
#include "base/trace_event/base_tracing.h"

namespace base {

MessagePumpFuchsia::ZxHandleWatchController::ZxHandleWatchController(
    const Location& from_here)
    : async_wait_t({}), created_from_location_(from_here) {}

MessagePumpFuchsia::ZxHandleWatchController::~ZxHandleWatchController() {
  const bool success = StopWatchingZxHandle();
  CHECK(success);
}

bool MessagePumpFuchsia::ZxHandleWatchController::WaitBegin() {
  DCHECK(!handler);
  async_wait_t::handler = &HandleSignal;

  zx_status_t status =
      async_begin_wait(weak_pump_->async_loop_->dispatcher(), this);
  if (status != ZX_OK) {
    ZX_DLOG(ERROR, status) << "async_begin_wait():"
                           << created_from_location_.ToString();
    async_wait_t::handler = nullptr;
    return false;
  }

  return true;
}

bool MessagePumpFuchsia::ZxHandleWatchController::StopWatchingZxHandle() {
  if (was_stopped_) {
    DCHECK(!*was_stopped_);
    *was_stopped_ = true;

    // |was_stopped_| points at a value stored on the stack, which will go out
    // of scope. MessagePumpFuchsia::Run() will reset it only if the value is
    // false. So we need to reset this pointer here as well, to make sure it's
    // not used again.
    was_stopped_ = nullptr;
  }

  // If the pump is gone then there is nothing to cancel.
  if (!weak_pump_) {
    return true;
  }

  if (!is_active()) {
    return true;
  }

  async_wait_t::handler = nullptr;

  zx_status_t result =
      async_cancel_wait(weak_pump_->async_loop_->dispatcher(), this);
  ZX_DLOG_IF(ERROR, result != ZX_OK, result)
      << "async_cancel_wait(): " << created_from_location_.ToString();
  return result == ZX_OK;
}

// static
void MessagePumpFuchsia::ZxHandleWatchController::HandleSignal(
    async_dispatcher_t* async,
    async_wait_t* wait,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  ZxHandleWatchController* controller =
      static_cast<ZxHandleWatchController*>(wait);
  DCHECK_EQ(controller->handler, &HandleSignal);

  // Inform ThreadController of this native work item for tracking purposes.
  // This call must precede the "ZxHandleSignal" trace event below as
  // BeginWorkItem() might generate a top-level trace event for the thread if
  // this is the first work item post-wakeup.
  Delegate::ScopedDoWorkItem scoped_do_work_item;
  if (controller->weak_pump_ && controller->weak_pump_->run_state_) {
    scoped_do_work_item =
        controller->weak_pump_->run_state_->delegate->BeginWorkItem();
  }

  TRACE_EVENT0("toplevel", "ZxHandleSignal");

  if (status != ZX_OK) {
    ZX_DLOG(WARNING, status) << "async wait failed: "
                             << controller->created_from_location_.ToString();
    return;
  }

  controller->handler = nullptr;

  // In the case of a persistent Watch, the Watch may be stopped and
  // potentially deleted by the caller within the callback, in which case
  // |controller| should not be accessed again, and we mustn't continue the
  // watch. We check for this with a bool on the stack, which the Watch
  // receives a pointer to.
  bool was_stopped = false;
  controller->was_stopped_ = &was_stopped;

  controller->watcher_->OnZxHandleSignalled(wait->object, signal->observed);

  if (was_stopped) {
    return;
  }

  controller->was_stopped_ = nullptr;

  if (controller->persistent_) {
    controller->WaitBegin();
  }
}

void MessagePumpFuchsia::FdWatchController::OnZxHandleSignalled(
    zx_handle_t handle,
    zx_signals_t signals) {
  uint32_t events;
  fdio_unsafe_wait_end(io_, signals, &events);

  // |events| can include other spurious things, in particular, that an fd
  // is writable, when we only asked to know when it was readable. In that
  // case, we don't want to call both the CanWrite and CanRead callback,
  // when the caller asked for only, for example, readable callbacks. So,
  // mask with the events that we actually wanted to know about.
  //
  // Note that errors are always included.
  const uint32_t desired_events = desired_events_ | FDIO_EVT_ERROR;
  const uint32_t filtered_events = events & desired_events;
  DCHECK_NE(filtered_events, 0u) << events << " & " << desired_events;

  // Each |watcher_| callback we invoke may stop or delete |this|. The pump has
  // set |was_stopped_| to point to a safe location on the calling stack, so we
  // can use that to detect being stopped mid-callback and avoid doing further
  // work that would touch |this|.
  bool* was_stopped = was_stopped_;
  if (filtered_events & FDIO_EVT_WRITABLE) {
    watcher_->OnFileCanWriteWithoutBlocking(fd_);
  }
  if (!*was_stopped && (filtered_events & FDIO_EVT_READABLE)) {
    watcher_->OnFileCanReadWithoutBlocking(fd_);
  }

  // Don't add additional work here without checking |*was_stopped_| again.
}

MessagePumpFuchsia::FdWatchController::FdWatchController(
    const Location& from_here)
    : FdWatchControllerInterface(from_here),
      ZxHandleWatchController(from_here) {}

MessagePumpFuchsia::FdWatchController::~FdWatchController() {
  const bool success = StopWatchingFileDescriptor();
  CHECK(success);
}

bool MessagePumpFuchsia::FdWatchController::WaitBegin() {
  // Refresh the |handle_| and |desired_signals_| from the fdio for the fd.
  // Some types of fdio map read/write events to different signals depending on
  // their current state, so we must do this every time we begin to wait.
  fdio_unsafe_wait_begin(io_, desired_events_, &object, &trigger);
  if (async_wait_t::object == ZX_HANDLE_INVALID) {
    DLOG(ERROR) << "fdio_wait_begin failed: "
                << ZxHandleWatchController::created_from_location_.ToString();
    return false;
  }

  return MessagePumpFuchsia::ZxHandleWatchController::WaitBegin();
}

bool MessagePumpFuchsia::FdWatchController::StopWatchingFileDescriptor() {
  bool success = StopWatchingZxHandle();
  if (io_) {
    fdio_unsafe_release(io_);
    io_ = nullptr;
  }
  return success;
}

MessagePumpFuchsia::MessagePumpFuchsia()
    : async_loop_(new async::Loop(&kAsyncLoopConfigAttachToCurrentThread)),
      weak_factory_(this) {}
MessagePumpFuchsia::~MessagePumpFuchsia() = default;

bool MessagePumpFuchsia::WatchFileDescriptor(int fd,
                                             bool persistent,
                                             int mode,
                                             FdWatchController* controller,
                                             FdWatcher* delegate) {
  DCHECK_GE(fd, 0);
  DCHECK(controller);
  DCHECK(delegate);

  const bool success = controller->StopWatchingFileDescriptor();
  CHECK(success);

  controller->fd_ = fd;
  controller->watcher_ = delegate;

  DCHECK(!controller->io_);
  controller->io_ = fdio_unsafe_fd_to_io(fd);
  if (!controller->io_) {
    DLOG(ERROR) << "Failed to get IO for FD";
    return false;
  }

  switch (mode) {
    case WATCH_READ:
      controller->desired_events_ = FDIO_EVT_READABLE;
      break;
    case WATCH_WRITE:
      controller->desired_events_ = FDIO_EVT_WRITABLE;
      break;
    case WATCH_READ_WRITE:
      controller->desired_events_ = FDIO_EVT_READABLE | FDIO_EVT_WRITABLE;
      break;
    default:
      NOTREACHED() << "unexpected mode: " << mode;
  }

  // Pass dummy |handle| and |signals| values to WatchZxHandle(). The real
  // values will be populated by FdWatchController::WaitBegin(), before actually
  // starting the wait operation.
  return WatchZxHandle(ZX_HANDLE_INVALID, persistent, 1, controller,
                       controller);
}

bool MessagePumpFuchsia::WatchZxHandle(zx_handle_t handle,
                                       bool persistent,
                                       zx_signals_t signals,
                                       ZxHandleWatchController* controller,
                                       ZxHandleWatcher* delegate) {
  DCHECK_NE(0u, signals);
  DCHECK(controller);
  DCHECK(delegate);

  // If the watch controller is active then WatchZxHandle() can be called only
  // for the same handle.
  DCHECK(handle == ZX_HANDLE_INVALID || !controller->is_active() ||
         handle == controller->async_wait_t::object);

  const bool success = controller->StopWatchingZxHandle();
  CHECK(success);

  controller->async_wait_t::object = handle;
  controller->persistent_ = persistent;
  controller->async_wait_t::trigger = signals;
  controller->watcher_ = delegate;

  controller->weak_pump_ = weak_factory_.GetWeakPtr();

  return controller->WaitBegin();
}

bool MessagePumpFuchsia::HandleIoEventsUntil(zx_time_t deadline) {
  zx_status_t status = async_loop_->Run(zx::time(deadline), /*once=*/true);
  switch (status) {
    // Return true if some tasks or events were dispatched or if the dispatcher
    // was stopped by ScheduleWork().
    case ZX_OK:
      return true;

    case ZX_ERR_CANCELED:
      async_loop_->ResetQuit();
      return true;

    case ZX_ERR_TIMED_OUT:
      return false;

    default:
      ZX_DLOG(FATAL, status) << "unexpected wait status";
      return false;
  }
}

void MessagePumpFuchsia::Run(Delegate* delegate) {
  RunState run_state(delegate);
  AutoReset<RunState*> auto_reset_run_state(&run_state_, &run_state);

  for (;;) {
    const Delegate::NextWorkInfo next_work_info = delegate->DoWork();
    if (run_state.should_quit) {
      break;
    }

    const bool did_handle_io_event = HandleIoEventsUntil(/*deadline=*/0);
    if (run_state.should_quit) {
      break;
    }

    bool attempt_more_work =
        next_work_info.is_immediate() || did_handle_io_event;
    if (attempt_more_work) {
      continue;
    }

    delegate->DoIdleWork();
    if (run_state.should_quit) {
      break;
    }

    delegate->BeforeWait();

    zx_time_t deadline = next_work_info.delayed_run_time.is_max()
                             ? ZX_TIME_INFINITE
                             : next_work_info.delayed_run_time.ToZxTime();

    HandleIoEventsUntil(deadline);
  }
}

void MessagePumpFuchsia::Quit() {
  CHECK(run_state_);
  run_state_->should_quit = true;
}

void MessagePumpFuchsia::ScheduleWork() {
  // Stop async_loop to let MessagePumpFuchsia::Run() handle message loop tasks.
  async_loop_->Quit();
}

void MessagePumpFuchsia::ScheduleDelayedWork(
    const Delegate::NextWorkInfo& next_work_info) {
  // Since this is always called from the same thread as Run(), there is nothing
  // to do as the loop is already running. It will wait in Run() with the
  // correct timeout when it's out of immediate tasks.
  // TODO(crbug.com/40594269): Consider removing ScheduleDelayedWork()
  // when all pumps function this way (bit.ly/merge-message-pump-do-work).
}

}  // namespace base
