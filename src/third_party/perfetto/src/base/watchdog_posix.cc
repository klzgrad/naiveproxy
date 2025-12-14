/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/base/platform.h"
#include "perfetto/ext/base/watchdog.h"

#if PERFETTO_BUILDFLAG(PERFETTO_WATCHDOG)

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <fstream>
#include <thread>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/thread_utils.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/crash_keys.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/utils.h"

namespace perfetto {
namespace base {

namespace {

constexpr uint32_t kDefaultPollingInterval = 30 * 1000;

base::CrashKey g_crash_key_reason("wdog_reason");

bool IsMultipleOf(uint32_t number, uint32_t divisor) {
  return number >= divisor && number % divisor == 0;
}

double MeanForArray(const uint64_t array[], size_t size) {
  uint64_t total = 0;
  for (size_t i = 0; i < size; i++) {
    total += array[i];
  }
  return static_cast<double>(total / size);
}

}  //  namespace

bool ReadProcStat(int fd, ProcStat* out) {
  char c[512];
  size_t c_pos = 0;
  while (c_pos < sizeof(c) - 1) {
    ssize_t rd = PERFETTO_EINTR(read(fd, c + c_pos, sizeof(c) - c_pos));
    if (rd < 0) {
      PERFETTO_ELOG("Failed to read stat file to enforce resource limits.");
      return false;
    }
    if (rd == 0)
      break;
    c_pos += static_cast<size_t>(rd);
  }
  PERFETTO_CHECK(c_pos < sizeof(c));
  c[c_pos] = '\0';

  if (sscanf(c,
             "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu "
             "%lu %*d %*d %*d %*d %*d %*d %*u %*u %ld",
             &out->utime, &out->stime, &out->rss_pages) != 3) {
    PERFETTO_ELOG("Invalid stat format: %s", c);
    return false;
  }
  return true;
}

Watchdog::Watchdog(uint32_t polling_interval_ms)
    : polling_interval_ms_(polling_interval_ms) {}

Watchdog::~Watchdog() {
  if (!thread_.joinable()) {
    PERFETTO_DCHECK(!enabled_);
    return;
  }
  PERFETTO_DCHECK(enabled_);
  enabled_ = false;

  // Rearm the timer to 1ns from now. This will cause the watchdog thread to
  // wakeup from the poll() and see |enabled_| == false.
  // This code path is used only in tests. In production code the watchdog is
  // a singleton and is never destroyed.
  struct itimerspec ts{};
  ts.it_value.tv_sec = 0;
  ts.it_value.tv_nsec = 1;
  timerfd_settime(*timer_fd_, /*flags=*/0, &ts, nullptr);

  thread_.join();
}

Watchdog* Watchdog::GetInstance() {
  static Watchdog* watchdog = new Watchdog(kDefaultPollingInterval);
  return watchdog;
}

// Can be called from any thread.
Watchdog::Timer Watchdog::CreateFatalTimer(uint32_t ms,
                                           WatchdogCrashReason crash_reason) {
  if (!enabled_.load(std::memory_order_relaxed))
    return Watchdog::Timer(this, 0, crash_reason);

  return Watchdog::Timer(this, ms, crash_reason);
}

// Can be called from any thread.
void Watchdog::AddFatalTimer(TimerData timer) {
  std::lock_guard<std::mutex> guard(mutex_);
  timers_.emplace_back(std::move(timer));
  RearmTimerFd_Locked();
}

// Can be called from any thread.
void Watchdog::RemoveFatalTimer(TimerData timer) {
  std::lock_guard<std::mutex> guard(mutex_);
  for (auto it = timers_.begin(); it != timers_.end(); it++) {
    if (*it == timer) {
      timers_.erase(it);
      break;  // Remove only one. Doesn't matter which one.
    }
  }
  RearmTimerFd_Locked();
}

void Watchdog::RearmTimerFd_Locked() {
  if (!enabled_)
    return;
  auto it = std::min_element(timers_.begin(), timers_.end());

  // We use one timerfd to handle all the outstanding |timers_|. Keep it armed
  // to the task expiring soonest.
  struct itimerspec ts{};
  if (it != timers_.end()) {
    ts.it_value = ToPosixTimespec(it->deadline);
  }
  // If |timers_| is empty (it == end()) |ts.it_value| will remain
  // zero-initialized and that will disarm the timer in the call below.
  int res = timerfd_settime(*timer_fd_, TFD_TIMER_ABSTIME, &ts, nullptr);
  PERFETTO_DCHECK(res == 0);
}

void Watchdog::Start() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (thread_.joinable()) {
    PERFETTO_DCHECK(enabled_);
  } else {
    PERFETTO_DCHECK(!enabled_);

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    // Kick the thread to start running but only on Android or Linux.
    timer_fd_.reset(
        timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
    if (!timer_fd_) {
      PERFETTO_PLOG(
          "timerfd_create failed, the Perfetto watchdog is not available");
      return;
    }
    enabled_ = true;
    RearmTimerFd_Locked();  // Deal with timers created before Start().
    thread_ = std::thread(&Watchdog::ThreadMain, this);
#endif
  }
}

void Watchdog::SetMemoryLimit(uint64_t bytes, uint32_t window_ms) {
  // Update the fields under the lock.
  std::lock_guard<std::mutex> guard(mutex_);

  PERFETTO_CHECK(IsMultipleOf(window_ms, polling_interval_ms_) || bytes == 0);

  size_t size = bytes == 0 ? 0 : window_ms / polling_interval_ms_ + 1;
  memory_window_bytes_.Reset(size);
  memory_limit_bytes_ = bytes;
}

void Watchdog::SetCpuLimit(uint32_t percentage, uint32_t window_ms) {
  std::lock_guard<std::mutex> guard(mutex_);

  PERFETTO_CHECK(percentage <= 100);
  PERFETTO_CHECK(IsMultipleOf(window_ms, polling_interval_ms_) ||
                 percentage == 0);

  size_t size = percentage == 0 ? 0 : window_ms / polling_interval_ms_ + 1;
  cpu_window_time_ticks_.Reset(size);
  cpu_limit_percentage_ = percentage;
}

void Watchdog::ThreadMain() {
  // Register crash keys explicitly to avoid running out of slots at crash time.
  g_crash_key_reason.Register();

  base::ScopedFile stat_fd(base::OpenFile("/proc/self/stat", O_RDONLY));
  if (!stat_fd) {
    PERFETTO_ELOG("Failed to open stat file to enforce resource limits.");
    return;
  }

  PERFETTO_DCHECK(timer_fd_);

  constexpr uint8_t kFdCount = 1;
  struct pollfd fds[kFdCount]{};
  fds[0].fd = *timer_fd_;
  fds[0].events = POLLIN;

  for (;;) {
    // We use the poll() timeout to drive the periodic ticks for the cpu/memory
    // checks. The only other case when the poll() unblocks is when we crash
    // (or have to quit via enabled_ == false, but that happens only in tests).
    platform::BeforeMaybeBlockingSyscall();
    auto ret = poll(fds, kFdCount, static_cast<int>(polling_interval_ms_));
    platform::AfterMaybeBlockingSyscall();
    if (!enabled_)
      return;
    if (ret < 0) {
      if (errno == ENOMEM || errno == EINTR) {
        // Should happen extremely rarely.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      PERFETTO_FATAL("watchdog poll() failed");
    }

    // If we get here either:
    // 1. poll() timed out, in which case we should process cpu/mem guardrails.
    // 2. A timer expired, in which case we shall crash.

    uint64_t expired = 0;  // Must be exactly 8 bytes.
    auto res = PERFETTO_EINTR(read(*timer_fd_, &expired, sizeof(expired)));
    PERFETTO_DCHECK((res < 0 && (errno == EAGAIN)) ||
                    (res == sizeof(expired) && expired > 0));
    const auto now = GetWallTimeMs();

    // Check if any of the timers expired.
    int tid_to_kill = 0;
    WatchdogCrashReason crash_reason{};
    {
      std::lock_guard<std::mutex> guard(mutex_);
      for (const auto& timer : timers_) {
        if (now >= timer.deadline) {
          tid_to_kill = timer.thread_id;
          crash_reason = timer.crash_reason;
          break;
        }
      }
    }

    if (tid_to_kill)
      SerializeLogsAndKillThread(tid_to_kill, crash_reason);

    // Check CPU and memory guardrails (if enabled).
    lseek(stat_fd.get(), 0, SEEK_SET);
    ProcStat stat;
    if (!ReadProcStat(stat_fd.get(), &stat))
      continue;
    uint64_t cpu_time = stat.utime + stat.stime;
    uint64_t rss_bytes =
        static_cast<uint64_t>(stat.rss_pages) * base::GetSysPageSize();

    bool threshold_exceeded = false;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (CheckMemory_Locked(rss_bytes) && !IsSyncMemoryTaggingEnabled()) {
        threshold_exceeded = true;
        crash_reason = WatchdogCrashReason::kMemGuardrail;
      } else if (CheckCpu_Locked(cpu_time)) {
        threshold_exceeded = true;
        crash_reason = WatchdogCrashReason::kCpuGuardrail;
      }
    }

    if (threshold_exceeded)
      SerializeLogsAndKillThread(getpid(), crash_reason);
  }
}

void Watchdog::SerializeLogsAndKillThread(int tid,
                                          WatchdogCrashReason crash_reason) {
  g_crash_key_reason.Set(static_cast<int>(crash_reason));

  // We are about to die. Serialize the logs into the crash buffer so the
  // debuggerd crash handler picks them up and attaches to the bugreport.
  // In the case of a PERFETTO_CHECK/PERFETTO_FATAL this is done in logging.h.
  // But in the watchdog case, we don't hit that codepath and must do ourselves.
  MaybeSerializeLastLogsForCrashReporting();

  // Send a SIGABRT to the thread that armed the timer. This is to see the
  // callstack of the thread that is stuck in a long task rather than the
  // watchdog thread.
  if (syscall(__NR_tgkill, getpid(), tid, SIGABRT) < 0) {
    // At this point the process must die. If for any reason the tgkill doesn't
    // work (e.g. the thread has disappeared), force a crash from here.
    abort();
  }

  if (disable_kill_failsafe_for_testing_)
    return;

  // The tgkill() above will take some milliseconds to cause a crash, as it
  // involves the kernel to queue the SIGABRT on the target thread (often the
  // main thread, which is != watchdog thread) and do a scheduling round.
  // If something goes wrong though (the target thread has signals masked or
  // is stuck in an uninterruptible+wakekill syscall) force quit from this
  // thread.
  std::this_thread::sleep_for(std::chrono::seconds(10));
  abort();
}

bool Watchdog::CheckMemory_Locked(uint64_t rss_bytes) {
  if (memory_limit_bytes_ == 0)
    return false;

  // Add the current stat value to the ring buffer and check that the mean
  // remains under our threshold.
  if (memory_window_bytes_.Push(rss_bytes)) {
    if (memory_window_bytes_.Mean() >
        static_cast<double>(memory_limit_bytes_)) {
      PERFETTO_ELOG(
          "Memory watchdog trigger. Memory window of %f bytes is above the "
          "%" PRIu64 " bytes limit.",
          memory_window_bytes_.Mean(), memory_limit_bytes_);
      return true;
    }
  }
  return false;
}

bool Watchdog::CheckCpu_Locked(uint64_t cpu_time) {
  if (cpu_limit_percentage_ == 0)
    return false;

  // Add the cpu time to the ring buffer.
  if (cpu_window_time_ticks_.Push(cpu_time)) {
    // Compute the percentage over the whole window and check that it remains
    // under the threshold.
    uint64_t difference_ticks = cpu_window_time_ticks_.NewestWhenFull() -
                                cpu_window_time_ticks_.OldestWhenFull();
    double window_interval_ticks =
        (static_cast<double>(WindowTimeForRingBuffer(cpu_window_time_ticks_)) /
         1000.0) *
        static_cast<double>(sysconf(_SC_CLK_TCK));
    double percentage = static_cast<double>(difference_ticks) /
                        static_cast<double>(window_interval_ticks) * 100;
    if (percentage > cpu_limit_percentage_) {
      PERFETTO_ELOG("CPU watchdog trigger. %f%% CPU use is above the %" PRIu32
                    "%% CPU limit.",
                    percentage, cpu_limit_percentage_);
      return true;
    }
  }
  return false;
}

uint32_t Watchdog::WindowTimeForRingBuffer(const WindowedInterval& window) {
  return static_cast<uint32_t>(window.size() - 1) * polling_interval_ms_;
}

bool Watchdog::WindowedInterval::Push(uint64_t sample) {
  // Add the sample to the current position in the ring buffer.
  buffer_[position_] = sample;

  // Update the position with next one circularily.
  position_ = (position_ + 1) % size_;

  // Set the filled flag the first time we wrap.
  filled_ = filled_ || position_ == 0;
  return filled_;
}

double Watchdog::WindowedInterval::Mean() const {
  return MeanForArray(buffer_.get(), size_);
}

void Watchdog::WindowedInterval::Clear() {
  position_ = 0;
  buffer_.reset(new uint64_t[size_]());
}

void Watchdog::WindowedInterval::Reset(size_t new_size) {
  position_ = 0;
  size_ = new_size;
  buffer_.reset(new_size == 0 ? nullptr : new uint64_t[new_size]());
}

Watchdog::Timer::Timer(Watchdog* watchdog,
                       uint32_t ms,
                       WatchdogCrashReason crash_reason)
    : watchdog_(watchdog) {
  if (!ms)
    return;  // No-op timer created when the watchdog is disabled.
  timer_data_.deadline = GetWallTimeMs() + std::chrono::milliseconds(ms);
  timer_data_.thread_id = GetThreadId();
  timer_data_.crash_reason = crash_reason;
  PERFETTO_DCHECK(watchdog_);
  watchdog_->AddFatalTimer(timer_data_);
}

Watchdog::Timer::~Timer() {
  if (timer_data_.deadline.count())
    watchdog_->RemoveFatalTimer(timer_data_);
}

Watchdog::Timer::Timer(Timer&& other) noexcept {
  watchdog_ = std::move(other.watchdog_);
  other.watchdog_ = nullptr;
  timer_data_ = std::move(other.timer_data_);
  other.timer_data_ = TimerData();
}

}  // namespace base
}  // namespace perfetto

#endif  // PERFETTO_BUILDFLAG(PERFETTO_WATCHDOG)
