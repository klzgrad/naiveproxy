// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_pump_android.h"

#include <android/looper.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <map>
#include <memory>
#include <utility>

#include "base/android/input_hint_checker.h"
#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/message_loop/io_watcher.h"
#include "base/notreached.h"
#include "base/numerics/safe_conversions.h"
#include "base/run_loop.h"
#include "base/task/task_features.h"
#include "base/time/time.h"
#include "build/build_config.h"

using base::android::InputHintChecker;
using base::android::InputHintResult;

namespace base {

namespace {

// https://crbug.com/873588. The stack may not be aligned when the ALooper calls
// into our code due to the inconsistent ABI on older Android OS versions.
//
// https://crbug.com/330761384#comment3. Calls from libutils.so into
// NonDelayedLooperCallback() and DelayedLooperCallback() confuse aarch64 builds
// with orderfile instrumentation causing incorrect value in
// __builtin_return_address(0). Disable instrumentation for them. TODO(pasko):
// Add these symbols to the orderfile manually or fix the builtin.
#if defined(ARCH_CPU_X86)
#define NO_INSTRUMENT_STACK_ALIGN \
  __attribute__((force_align_arg_pointer, no_instrument_function))
#else
#define NO_INSTRUMENT_STACK_ALIGN __attribute__((no_instrument_function))
#endif

NO_INSTRUMENT_STACK_ALIGN int NonDelayedLooperCallback(int fd,
                                                       int events,
                                                       void* data) {
  if (events & ALOOPER_EVENT_HANGUP) {
    return 0;
  }

  DCHECK(events & ALOOPER_EVENT_INPUT);
  MessagePumpAndroid* pump = reinterpret_cast<MessagePumpAndroid*>(data);
  pump->OnNonDelayedLooperCallback();
  return 1;  // continue listening for events
}

NO_INSTRUMENT_STACK_ALIGN int DelayedLooperCallback(int fd,
                                                    int events,
                                                    void* data) {
  if (events & ALOOPER_EVENT_HANGUP) {
    return 0;
  }

  DCHECK(events & ALOOPER_EVENT_INPUT);
  MessagePumpAndroid* pump = reinterpret_cast<MessagePumpAndroid*>(data);
  pump->OnDelayedLooperCallback();
  return 1;  // continue listening for events
}

// A bit added to the |non_delayed_fd_| to keep it signaled when we yield to
// native work below.
constexpr uint64_t kTryNativeWorkBeforeIdleBit = uint64_t(1) << 32;

std::atomic_bool g_fast_to_sleep = false;

// Implements IOWatcher to allow any MessagePumpAndroid thread to watch
// arbitrary file descriptors for I/O events.
class IOWatcherImpl : public IOWatcher {
 public:
  explicit IOWatcherImpl(ALooper* looper) : looper_(looper) {}

  ~IOWatcherImpl() override {
    for (auto& [fd, watches] : watched_fds_) {
      ALooper_removeFd(looper_, fd);
      if (auto read_watch = std::exchange(watches.read_watch, nullptr)) {
        read_watch->Detach();
      }
      if (auto write_watch = std::exchange(watches.write_watch, nullptr)) {
        write_watch->Detach();
      }
    }
  }

  // IOWatcher:
  std::unique_ptr<IOWatcher::FdWatch> WatchFileDescriptorImpl(
      int fd,
      FdWatchDuration duration,
      FdWatchMode mode,
      IOWatcher::FdWatcher& watcher,
      const Location& location) override {
    auto& watches = watched_fds_[fd];
    auto watch = std::make_unique<FdWatchImpl>(*this, fd, duration, watcher);
    if (mode == FdWatchMode::kRead || mode == FdWatchMode::kReadWrite) {
      CHECK(!watches.read_watch) << "Only one watch per FD per condition.";
      watches.read_watch = watch.get();
    }
    if (mode == FdWatchMode::kWrite || mode == FdWatchMode::kReadWrite) {
      CHECK(!watches.write_watch) << "Only one watch per FD per condition.";
      watches.write_watch = watch.get();
    }

    const int events = (watches.read_watch ? ALOOPER_EVENT_INPUT : 0) |
                       (watches.write_watch ? ALOOPER_EVENT_OUTPUT : 0);
    ALooper_addFd(looper_, fd, 0, events, &OnFdIoEvent, this);
    return watch;
  }

 private:
  // Scopes the maximum lifetime of an FD watch started by WatchFileDescriptor.
  class FdWatchImpl : public FdWatch {
   public:
    FdWatchImpl(IOWatcherImpl& io_watcher,
                int fd,
                FdWatchDuration duration,
                FdWatcher& fd_watcher)
        : fd_(fd),
          duration_(duration),
          fd_watcher_(fd_watcher),
          io_watcher_(&io_watcher) {}

    ~FdWatchImpl() override {
      Stop();
      if (destruction_flag_) {
        *destruction_flag_ = true;
      }
    }

    void set_destruction_flag(bool* flag) { destruction_flag_ = flag; }
    int fd() const { return fd_; }
    FdWatcher& fd_watcher() const { return *fd_watcher_; }

    bool is_persistent() const {
      return duration_ == FdWatchDuration::kPersistent;
    }

    void Detach() { io_watcher_ = nullptr; }

    void Stop() {
      if (io_watcher_) {
        std::exchange(io_watcher_, nullptr)->StopWatching(*this);
      }
    }

   private:
    const int fd_;
    const FdWatchDuration duration_;
    raw_ref<FdWatcher> fd_watcher_;
    raw_ptr<IOWatcherImpl> io_watcher_;

    // If non-null during destruction, the pointee is set to true. Used to
    // detect reentrant destruction during dispatch.
    raw_ptr<bool> destruction_flag_ = nullptr;
  };

  enum class EventResult {
    kStopWatching,
    kKeepWatching,
  };

  static NO_INSTRUMENT_STACK_ALIGN int OnFdIoEvent(int fd,
                                                   int events,
                                                   void* data) {
    switch (static_cast<IOWatcherImpl*>(data)->HandleEvent(fd, events)) {
      case EventResult::kStopWatching:
        return 0;
      case EventResult::kKeepWatching:
        return 1;
    }
  }

  EventResult HandleEvent(int fd, int events) {
    // NOTE: It is possible for Looper to dispatch one last event for `fd`
    // *after* we have removed the FD from the Looper - for example if multiple
    // FDs wake the thread at the same time, and a handler for another FD runs
    // first and removes the watch for `fd`; this callback will have already
    // been queued for `fd` and will still run. As such, we must gracefully
    // tolerate receiving a callback for an FD that is no longer watched.
    auto it = watched_fds_.find(fd);
    if (it == watched_fds_.end()) {
      return EventResult::kStopWatching;
    }

    auto& watches = it->second;
    const bool is_readable =
        events & (ALOOPER_EVENT_INPUT | ALOOPER_EVENT_HANGUP);
    const bool is_writable =
        events & (ALOOPER_EVENT_OUTPUT | ALOOPER_EVENT_HANGUP);
    auto* read_watch = watches.read_watch.get();
    auto* write_watch = watches.write_watch.get();

    // Any event dispatch can stop any number of watches, so we're careful to
    // set up destruction observation before dispatching anything.
    bool read_watch_destroyed = false;
    bool write_watch_destroyed = false;
    bool fd_removed = false;
    if (read_watch) {
      read_watch->set_destruction_flag(&read_watch_destroyed);
    }
    if (write_watch && read_watch != write_watch) {
      write_watch->set_destruction_flag(&write_watch_destroyed);
    }
    watches.removed_flag = &fd_removed;

    bool did_observe_one_shot_read = false;
    if (read_watch && is_readable) {
      DCHECK_EQ(read_watch->fd(), fd);
      did_observe_one_shot_read = !read_watch->is_persistent();
      read_watch->fd_watcher().OnFdReadable(fd);
      if (!read_watch_destroyed && did_observe_one_shot_read) {
        read_watch->Stop();
      }
    }

    // If the read and write watches are the same object, it may have been
    // destroyed; or it may have been a one-shot watch already consumed by a
    // read above. In either case we inhibit write dispatch.
    if (read_watch == write_watch &&
        (read_watch_destroyed || did_observe_one_shot_read)) {
      write_watch = nullptr;
    }

    if (write_watch && is_writable && !write_watch_destroyed) {
      DCHECK_EQ(write_watch->fd(), fd);
      const bool is_persistent = write_watch->is_persistent();
      write_watch->fd_watcher().OnFdWritable(fd);
      if (!write_watch_destroyed && !is_persistent) {
        write_watch->Stop();
      }
    }

    if (read_watch && !read_watch_destroyed) {
      read_watch->set_destruction_flag(nullptr);
    }
    if (write_watch && !write_watch_destroyed) {
      write_watch->set_destruction_flag(nullptr);
    }

    if (fd_removed) {
      return EventResult::kStopWatching;
    }

    watches.removed_flag = nullptr;
    return EventResult::kKeepWatching;
  }

  void StopWatching(FdWatchImpl& watch) {
    const int fd = watch.fd();
    auto it = watched_fds_.find(fd);
    if (it == watched_fds_.end()) {
      return;
    }

    WatchPair& watches = it->second;
    if (watches.read_watch == &watch) {
      watches.read_watch = nullptr;
    }
    if (watches.write_watch == &watch) {
      watches.write_watch = nullptr;
    }

    const int remaining_events =
        (watches.read_watch ? ALOOPER_EVENT_INPUT : 0) |
        (watches.write_watch ? ALOOPER_EVENT_OUTPUT : 0);
    if (remaining_events) {
      ALooper_addFd(looper_, fd, 0, remaining_events, &OnFdIoEvent, this);
      return;
    }

    ALooper_removeFd(looper_, fd);
    if (watches.removed_flag) {
      *watches.removed_flag = true;
    }
    watched_fds_.erase(it);
  }

 private:
  const raw_ptr<ALooper> looper_;

  // The set of active FdWatches. Note that each FD may have up to two active
  // watches only - one for read and one for write. No two FdWatches can watch
  // the same FD for the same signal. `read_watch` and `write_watch` may point
  // to the same object.
  struct WatchPair {
    raw_ptr<FdWatchImpl> read_watch = nullptr;
    raw_ptr<FdWatchImpl> write_watch = nullptr;

    // If non-null when this WatchPair is removed, the pointee is set to true.
    // Used to track reentrant map mutations during dispatch.
    raw_ptr<bool> removed_flag = nullptr;
  };
  std::map<int, WatchPair> watched_fds_;
};

}  // namespace

MessagePumpAndroid::MessagePumpAndroid()
    : env_(base::android::AttachCurrentThread()) {
  // The Android native ALooper uses epoll to poll our file descriptors and wake
  // us up. We use a simple level-triggered eventfd to signal that non-delayed
  // work is available, and a timerfd to signal when delayed work is ready to
  // be run.
  non_delayed_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  CHECK_NE(non_delayed_fd_, -1);
  DCHECK_EQ(TimeTicks::GetClock(), TimeTicks::Clock::LINUX_CLOCK_MONOTONIC);

  delayed_fd_ = checked_cast<int>(
      timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC));
  CHECK_NE(delayed_fd_, -1);

  looper_ = ALooper_prepare(0);
  DCHECK(looper_);
  // Add a reference to the looper so it isn't deleted on us.
  ALooper_acquire(looper_);
  ALooper_addFd(looper_, non_delayed_fd_, 0, ALOOPER_EVENT_INPUT,
                &NonDelayedLooperCallback, reinterpret_cast<void*>(this));
  ALooper_addFd(looper_, delayed_fd_, 0, ALOOPER_EVENT_INPUT,
                &DelayedLooperCallback, reinterpret_cast<void*>(this));
}

MessagePumpAndroid::~MessagePumpAndroid() {
  DCHECK_EQ(ALooper_forThread(), looper_);
  io_watcher_.reset();
  ALooper_removeFd(looper_, non_delayed_fd_);
  ALooper_removeFd(looper_, delayed_fd_);
  ALooper_release(looper_);
  looper_ = nullptr;

  close(non_delayed_fd_);
  close(delayed_fd_);
}

void MessagePumpAndroid::InitializeFeatures() {
  g_fast_to_sleep = base::FeatureList::IsEnabled(kPumpFastToSleepAndroid);
}

void MessagePumpAndroid::OnDelayedLooperCallback() {
  OnReturnFromLooper();
  // There may be non-Chromium callbacks on the same ALooper which may have left
  // a pending exception set, and ALooper does not check for this between
  // callbacks. Check here, and if there's already an exception, just skip this
  // iteration without clearing the fd. If the exception ends up being non-fatal
  // then we'll just get called again on the next polling iteration.
  if (base::android::HasException(env_)) {
    return;
  }

  // ALooper_pollOnce may call this after Quit() if OnNonDelayedLooperCallback()
  // resulted in Quit() in the same round.
  if (ShouldQuit()) {
    return;
  }

  // Clear the fd.
  uint64_t value;
  long ret = read(delayed_fd_, &value, sizeof(value));

  // TODO(mthiesse): Figure out how it's possible to hit EAGAIN here.
  // According to http://man7.org/linux/man-pages/man2/timerfd_create.2.html
  // EAGAIN only happens if no timer has expired. Also according to the man page
  // poll only returns readable when a timer has expired. So this function will
  // only be called when a timer has expired, but reading reveals no timer has
  // expired...
  // Quit() and ScheduleDelayedWork() are the only other functions that touch
  // the timerfd, and they both run on the same thread as this callback, so
  // there are no obvious timing or multi-threading related issues.
  DPCHECK(ret >= 0 || errno == EAGAIN);
  DoDelayedLooperWork();
}

void MessagePumpAndroid::DoDelayedLooperWork() {
  delayed_scheduled_time_.reset();

  Delegate::NextWorkInfo next_work_info = delegate_->DoWork();

  if (ShouldQuit()) {
    return;
  }

  if (next_work_info.is_immediate()) {
    ScheduleWork();
    return;
  }

  delegate_->DoIdleWork();
  if (!next_work_info.delayed_run_time.is_max()) {
    ScheduleDelayedWork(next_work_info);
  }
}

void MessagePumpAndroid::OnNonDelayedLooperCallback() {
  OnReturnFromLooper();
  // There may be non-Chromium callbacks on the same ALooper which may have left
  // a pending exception set, and ALooper does not check for this between
  // callbacks. Check here, and if there's already an exception, just skip this
  // iteration without clearing the fd. If the exception ends up being non-fatal
  // then we'll just get called again on the next polling iteration.
  if (base::android::HasException(env_)) {
    return;
  }

  // ALooper_pollOnce may call this after Quit() if OnDelayedLooperCallback()
  // resulted in Quit() in the same round.
  if (ShouldQuit()) {
    return;
  }

  // We're about to process all the work requested by ScheduleWork().
  // MessagePump users are expected to do their best not to invoke
  // ScheduleWork() again before DoWork() returns a non-immediate
  // NextWorkInfo below. Hence, capturing the file descriptor's value now and
  // resetting its contents to 0 should be okay. The value currently stored
  // should be greater than 0 since work having been scheduled is the reason
  // we're here. See http://man7.org/linux/man-pages/man2/eventfd.2.html
  uint64_t value = 0;
  long ret = read(non_delayed_fd_, &value, sizeof(value));
  DPCHECK(ret >= 0);
  DCHECK_GT(value, 0U);
  bool do_idle_work = value == kTryNativeWorkBeforeIdleBit;
  DoNonDelayedLooperWork(do_idle_work);
}

void MessagePumpAndroid::DoNonDelayedLooperWork(bool do_idle_work) {
  // Note: We can't skip DoWork() even if |do_idle_work| is true here (i.e. no
  // additional ScheduleWork() since yielding to native) as delayed tasks might
  // have come in and we need to re-sample |next_work_info|.

  // Runs all application tasks scheduled to run.
  Delegate::NextWorkInfo next_work_info;
  do {
    if (ShouldQuit()) {
      return;
    }

    next_work_info = delegate_->DoWork();

    // As an optimization, yield to the Looper when input events are waiting to
    // be handled. In some cases input events can remain undetected. Such "input
    // hint false negatives" happen, for example, during initialization, in
    // multi-window cases, or when a previous value is cached to throttle
    // polling the input channel.
    if (is_type_ui_ && next_work_info.is_immediate() &&
        InputHintChecker::HasInput()) {
      InputHintChecker::GetInstance().set_is_after_input_yield(true);
      ScheduleWork();
      return;
    }
  } while (next_work_info.is_immediate());

  // Do not resignal |non_delayed_fd_| if we're quitting (this pump doesn't
  // allow nesting so needing to resume in an outer loop is not an issue
  // either).
  if (ShouldQuit()) {
    return;
  }

  // Under the fast to sleep feature, `do_idle_work` is ignored, and the pump
  // will always "sleep" after finishing all its work items.
  if (!g_fast_to_sleep) {
    // Before declaring this loop idle, yield to native work items and arrange
    // to be called again (unless we're already in that second call).
    if (!do_idle_work) {
      ScheduleWorkInternal(/*do_idle_work=*/true);
      return;
    }

    // We yielded to native work items already and they didn't generate a
    // ScheduleWork() request so we can declare idleness. It's possible for a
    // ScheduleWork() request to come in racily while this method unwinds, this
    // is fine and will merely result in it being re-invoked shortly after it
    // returns.
    // TODO(scheduler-dev): this doesn't account for tasks that don't ever call
    // SchedulerWork() but still keep the system non-idle (e.g., the Java
    // Handler API). It would be better to add an API to query the presence of
    // native tasks instead of relying on yielding once +
    // kTryNativeWorkBeforeIdleBit.
    DCHECK(do_idle_work);
  }

  if (ShouldQuit()) {
    return;
  }

  // Do the idle work.
  //
  // At this point, the Java Looper might not be idle. It is possible to skip
  // idle work if !MessageQueue.isIdle(), but this check is not very accurate
  // because the MessageQueue does not know about the additional tasks
  // potentially waiting in the Looper.
  //
  // Note that this won't cause us to fail to run java tasks using QuitWhenIdle,
  // as the JavaHandlerThread will finish running all currently scheduled tasks
  // before it quits. Also note that we can't just add an idle callback to the
  // java looper, as that will fire even if application tasks are still queued
  // up.
  delegate_->DoIdleWork();
  if (!next_work_info.delayed_run_time.is_max()) {
    ScheduleDelayedWork(next_work_info);
  }
}

void MessagePumpAndroid::Run(Delegate* delegate) {
  NOTREACHED() << "Unexpected call to Run()";
}

void MessagePumpAndroid::Attach(Delegate* delegate) {
  DCHECK(!quit_);

  // Since the Looper is controlled by the UI thread or JavaHandlerThread, we
  // can't use Run() like we do on other platforms or we would prevent Java
  // tasks from running. Instead we create and initialize a run loop here, then
  // return control back to the Looper.

  SetDelegate(delegate);
  run_loop_ = std::make_unique<RunLoop>();
  // Since the RunLoop was just created above, BeforeRun should be guaranteed to
  // return true (it only returns false if the RunLoop has been Quit already).
  CHECK(run_loop_->BeforeRun());
}

void MessagePumpAndroid::Quit() {
  if (quit_) {
    return;
  }

  quit_ = true;

  int64_t value;
  // Clear any pending timer.
  read(delayed_fd_, &value, sizeof(value));
  // Clear the eventfd.
  read(non_delayed_fd_, &value, sizeof(value));

  if (run_loop_) {
    run_loop_->AfterRun();
    run_loop_ = nullptr;
  }
  if (on_quit_callback_) {
    std::move(on_quit_callback_).Run();
  }
}

void MessagePumpAndroid::ScheduleWork() {
  ScheduleWorkInternal(/*do_idle_work=*/false);
}

void MessagePumpAndroid::ScheduleWorkInternal(bool do_idle_work) {
  // Write (add) |value| to the eventfd. This tells the Looper to wake up and
  // call our callback, allowing us to run tasks. This also allows us to detect,
  // when we clear the fd, whether additional work was scheduled after we
  // finished performing work, but before we cleared the fd, as we'll read back
  // >=2 instead of 1 in that case. See the eventfd man pages
  // (http://man7.org/linux/man-pages/man2/eventfd.2.html) for details on how
  // the read and write APIs for this file descriptor work, specifically without
  // EFD_SEMAPHORE.
  // Note: Calls with |do_idle_work| set to true may race with potential calls
  // where the parameter is false. This is fine as write() is adding |value|,
  // not overwriting the existing value, and as such racing calls would merely
  // have their values added together. Since idle work is only executed when the
  // value read equals kTryNativeWorkBeforeIdleBit, a race would prevent idle
  // work from being run and trigger another call to this method with
  // |do_idle_work| set to true.
  uint64_t value = do_idle_work ? kTryNativeWorkBeforeIdleBit : 1;
  long ret = write(non_delayed_fd_, &value, sizeof(value));
  DPCHECK(ret >= 0);
}

void MessagePumpAndroid::OnReturnFromLooper() {
  if (!is_type_ui_) {
    return;
  }
  auto& checker = InputHintChecker::GetInstance();
  if (checker.is_after_input_yield()) {
    InputHintChecker::RecordInputHintResult(InputHintResult::kBackToNative);
  }
  checker.set_is_after_input_yield(false);
}

void MessagePumpAndroid::ScheduleDelayedWork(
    const Delegate::NextWorkInfo& next_work_info) {
  if (ShouldQuit()) {
    return;
  }

  if (delayed_scheduled_time_ &&
      *delayed_scheduled_time_ == next_work_info.delayed_run_time) {
    return;
  }

  DCHECK(!next_work_info.is_immediate());
  delayed_scheduled_time_ = next_work_info.delayed_run_time;
  int64_t nanos =
      next_work_info.delayed_run_time.since_origin().InNanoseconds();
  struct itimerspec ts;
  ts.it_interval.tv_sec = 0;  // Don't repeat.
  ts.it_interval.tv_nsec = 0;
  ts.it_value.tv_sec =
      static_cast<time_t>(nanos / TimeTicks::kNanosecondsPerSecond);
  ts.it_value.tv_nsec = nanos % TimeTicks::kNanosecondsPerSecond;

  long ret = timerfd_settime(delayed_fd_, TFD_TIMER_ABSTIME, &ts, nullptr);
  DPCHECK(ret >= 0);
}

IOWatcher* MessagePumpAndroid::GetIOWatcher() {
  if (!io_watcher_) {
    io_watcher_ = std::make_unique<IOWatcherImpl>(looper_);
  }
  return io_watcher_.get();
}

void MessagePumpAndroid::QuitWhenIdle(base::OnceClosure callback) {
  DCHECK(!on_quit_callback_);
  DCHECK(run_loop_);
  on_quit_callback_ = std::move(callback);
  run_loop_->QuitWhenIdle();
  // Pump the loop in case we're already idle.
  ScheduleWork();
}

MessagePump::Delegate* MessagePumpAndroid::SetDelegate(Delegate* delegate) {
  return std::exchange(delegate_, delegate);
}

bool MessagePumpAndroid::SetQuit(bool quit) {
  return std::exchange(quit_, quit);
}

}  // namespace base
