// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/hang_watcher.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/debug/alias.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/debug/leak_annotations.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/power_monitor/power_monitor.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/threading_features.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "base/trace_event/base_tracing.h"
#include "build/build_config.h"

namespace base {

namespace {

// Defines how much logging happens when the HangWatcher monitors the threads.
// Logging levels are set per thread type through Finch. It's important that
// the order of the enum members stay the same and that their numerical
// values be in increasing order. The implementation of
// ThreadTypeLoggingLevelGreaterOrEqual() depends on it.
enum class LoggingLevel { kNone = 0, kUmaOnly = 1, kUmaAndCrash = 2 };

HangWatcher* g_instance = nullptr;
constinit thread_local internal::HangWatchState* hang_watch_state = nullptr;
std::atomic<bool> g_use_hang_watcher{false};
std::atomic<HangWatcher::ProcessType> g_hang_watcher_process_type{
    HangWatcher::ProcessType::kBrowserProcess};

std::atomic<LoggingLevel> g_threadpool_log_level{LoggingLevel::kNone};
std::atomic<LoggingLevel> g_io_thread_log_level{LoggingLevel::kNone};
std::atomic<LoggingLevel> g_main_thread_log_level{LoggingLevel::kNone};

// Indicates whether HangWatcher::Run() should return after the next monitoring.
std::atomic<bool> g_keep_monitoring{true};

// If true, indicates that this process's shutdown sequence has started. Once
// flipped to true, cannot be un-flipped.
std::atomic<bool> g_shutting_down{false};

// Emits the hung thread count histogram. |count| is the number of threads
// of type |thread_type| that were hung or became hung during the last
// monitoring window. This function should be invoked for each thread type
// encountered on each call to Monitor(). `sample_ticks` is the time at which
// the sample was taken and `monitoring_period` is the interval being sampled.
void LogStatusHistogram(HangWatcher::ThreadType thread_type,
                        int count,
                        TimeTicks sample_ticks,
                        TimeDelta monitoring_period) {
  // In the case of unique threads like the IO or UI/Main thread a count does
  // not make sense.
  const bool any_thread_hung = count >= 1;
  const bool shutting_down = g_shutting_down.load(std::memory_order_relaxed);

  // Uses histogram macros instead of functions. This increases binary size
  // slightly, but runs slightly faster. These histograms are logged pretty
  // often, so we prefer improving runtime.
  const HangWatcher::ProcessType process_type =
      g_hang_watcher_process_type.load(std::memory_order_relaxed);
  switch (process_type) {
    case HangWatcher::ProcessType::kUnknownProcess:
      break;

    case HangWatcher::ProcessType::kBrowserProcess:
      switch (thread_type) {
        case HangWatcher::ThreadType::kIOThread:
          if (shutting_down) {
            UMA_HISTOGRAM_BOOLEAN(
                "HangWatcher.IsThreadHung.BrowserProcess.IOThread.Shutdown",
                any_thread_hung);
          } else {
            UMA_HISTOGRAM_BOOLEAN(
                "HangWatcher.IsThreadHung.BrowserProcess.IOThread.Normal",
                any_thread_hung);
          }
          break;
        case HangWatcher::ThreadType::kMainThread:
          if (shutting_down) {
            UMA_HISTOGRAM_BOOLEAN(
                "HangWatcher.IsThreadHung.BrowserProcess.UIThread.Shutdown",
                any_thread_hung);
          } else {
            UMA_HISTOGRAM_BOOLEAN(
                "HangWatcher.IsThreadHung.BrowserProcess.UIThread.Normal",
                any_thread_hung);
          }
          break;
        case HangWatcher::ThreadType::kThreadPoolThread:
          // Not recorded for now.
          break;
      }
      break;

    case HangWatcher::ProcessType::kGPUProcess:
      // Not recorded for now.
      CHECK(!shutting_down);
      break;

    case HangWatcher::ProcessType::kRendererProcess:
      CHECK(!shutting_down);
      switch (thread_type) {
        case HangWatcher::ThreadType::kIOThread:
          UMA_HISTOGRAM_SPLIT_BY_PROCESS_PRIORITY(
              UMA_HISTOGRAM_BOOLEAN, sample_ticks, monitoring_period,
              "HangWatcher.IsThreadHung.RendererProcess.IOThread",
              any_thread_hung);
          break;
        case HangWatcher::ThreadType::kMainThread:
          UMA_HISTOGRAM_SPLIT_BY_PROCESS_PRIORITY(
              UMA_HISTOGRAM_BOOLEAN, sample_ticks, monitoring_period,
              "HangWatcher.IsThreadHung.RendererProcess.MainThread",
              any_thread_hung);
          break;
        case HangWatcher::ThreadType::kThreadPoolThread:
          // Not recorded for now.
          break;
      }
      break;

    case HangWatcher::ProcessType::kUtilityProcess:
      CHECK(!shutting_down);
      switch (thread_type) {
        case HangWatcher::ThreadType::kIOThread:
          UMA_HISTOGRAM_BOOLEAN(
              "HangWatcher.IsThreadHung.UtilityProcess.IOThread",
              any_thread_hung);
          break;
        case HangWatcher::ThreadType::kMainThread:
          UMA_HISTOGRAM_BOOLEAN(
              "HangWatcher.IsThreadHung.UtilityProcess.MainThread",
              any_thread_hung);
          break;
        case HangWatcher::ThreadType::kThreadPoolThread:
          // Not recorded for now.
          break;
      }
      break;
  }
}

// Returns true if |thread_type| was configured through Finch to have a logging
// level that is equal to or exceeds |logging_level|.
bool ThreadTypeLoggingLevelGreaterOrEqual(HangWatcher::ThreadType thread_type,
                                          LoggingLevel logging_level) {
  switch (thread_type) {
    case HangWatcher::ThreadType::kIOThread:
      return g_io_thread_log_level.load(std::memory_order_relaxed) >=
             logging_level;
    case HangWatcher::ThreadType::kMainThread:
      return g_main_thread_log_level.load(std::memory_order_relaxed) >=
             logging_level;
    case HangWatcher::ThreadType::kThreadPoolThread:
      return g_threadpool_log_level.load(std::memory_order_relaxed) >=
             logging_level;
  }
}

}  // namespace

// Enables the HangWatcher. When disabled, the HangWatcher thread should not be
// started. Enabled by default only on platforms where the generated data is
// used, to avoid unnecessary overhead.
BASE_FEATURE(kEnableHangWatcher,
             "EnableHangWatcher",
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_LINUX)
             FEATURE_ENABLED_BY_DEFAULT
#else
             FEATURE_DISABLED_BY_DEFAULT
#endif
);

// Browser process.
// Note: Do not use the prepared macro as of no need for a local cache.
constexpr base::FeatureParam<int> kIOThreadLogLevel{
    &kEnableHangWatcher, "io_thread_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};
constexpr base::FeatureParam<int> kUIThreadLogLevel{
    &kEnableHangWatcher, "ui_thread_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};
constexpr base::FeatureParam<int> kThreadPoolLogLevel{
    &kEnableHangWatcher, "threadpool_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};

// GPU process.
// Note: Do not use the prepared macro as of no need for a local cache.
constexpr base::FeatureParam<int> kGPUProcessIOThreadLogLevel{
    &kEnableHangWatcher, "gpu_process_io_thread_log_level",
    static_cast<int>(LoggingLevel::kNone)};
constexpr base::FeatureParam<int> kGPUProcessMainThreadLogLevel{
    &kEnableHangWatcher, "gpu_process_main_thread_log_level",
    static_cast<int>(LoggingLevel::kNone)};
constexpr base::FeatureParam<int> kGPUProcessThreadPoolLogLevel{
    &kEnableHangWatcher, "gpu_process_threadpool_log_level",
    static_cast<int>(LoggingLevel::kNone)};

// Renderer process.
// Note: Do not use the prepared macro as of no need for a local cache.
constexpr base::FeatureParam<int> kRendererProcessIOThreadLogLevel{
    &kEnableHangWatcher, "renderer_process_io_thread_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};
constexpr base::FeatureParam<int> kRendererProcessMainThreadLogLevel{
    &kEnableHangWatcher, "renderer_process_main_thread_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};
constexpr base::FeatureParam<int> kRendererProcessThreadPoolLogLevel{
    &kEnableHangWatcher, "renderer_process_threadpool_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};

// Utility process.
// Note: Do not use the prepared macro as of no need for a local cache.
constexpr base::FeatureParam<int> kUtilityProcessIOThreadLogLevel{
    &kEnableHangWatcher, "utility_process_io_thread_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};
constexpr base::FeatureParam<int> kUtilityProcessMainThreadLogLevel{
    &kEnableHangWatcher, "utility_process_main_thread_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};
constexpr base::FeatureParam<int> kUtilityProcessThreadPoolLogLevel{
    &kEnableHangWatcher, "utility_process_threadpool_log_level",
    static_cast<int>(LoggingLevel::kUmaOnly)};

constexpr const char* kThreadName = "HangWatcher";

// The time that the HangWatcher thread will sleep for between calls to
// Monitor(). Increasing or decreasing this does not modify the type of hangs
// that can be detected. It instead increases the probability that a call to
// Monitor() will happen at the right time to catch a hang. This has to be
// balanced with power/cpu use concerns as busy looping would catch amost all
// hangs but present unacceptable overhead. NOTE: If this period is ever changed
// then all metrics that depend on it like
// HangWatcher.IsThreadHung need to be updated.
constexpr auto kMonitoringPeriod = base::Seconds(10);

WatchHangsInScope::WatchHangsInScope(TimeDelta timeout) {
  internal::HangWatchState* current_hang_watch_state =
      HangWatcher::IsEnabled()
          ? internal::HangWatchState::GetHangWatchStateForCurrentThread()
          : nullptr;

  DCHECK(timeout >= base::TimeDelta()) << "Negative timeouts are invalid.";

  // Thread is not monitored, noop.
  if (!current_hang_watch_state) {
    took_effect_ = false;
    return;
  }

#if DCHECK_IS_ON()
  previous_watch_hangs_in_scope_ =
      current_hang_watch_state->GetCurrentWatchHangsInScope();
  current_hang_watch_state->SetCurrentWatchHangsInScope(this);
#endif

  auto [old_flags, old_deadline] =
      current_hang_watch_state->GetFlagsAndDeadline();

  // TODO(crbug.com/40111620): Check whether we are over deadline already for
  // the previous WatchHangsInScope here by issuing only one TimeTicks::Now()
  // and resuing the value.

  previous_deadline_ = old_deadline;
  TimeTicks deadline = TimeTicks::Now() + timeout;
  current_hang_watch_state->SetDeadline(deadline);
  current_hang_watch_state->IncrementNestingLevel();

  const bool hangs_ignored_for_current_scope =
      internal::HangWatchDeadline::IsFlagSet(
          internal::HangWatchDeadline::Flag::kIgnoreCurrentWatchHangsInScope,
          old_flags);

  // If the current WatchHangsInScope is ignored, temporarily reactivate hang
  // watching for newly created WatchHangsInScopes. On exiting hang watching
  // is suspended again to return to the original state.
  if (hangs_ignored_for_current_scope) {
    current_hang_watch_state->UnsetIgnoreCurrentWatchHangsInScope();
    set_hangs_ignored_on_exit_ = true;
  }
}

WatchHangsInScope::~WatchHangsInScope() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // If hang watching was not enabled at construction time there is nothing to
  // validate or undo.
  if (!took_effect_) {
    return;
  }

  // If the thread was unregistered since construction there is also nothing to
  // do.
  auto* const state =
      internal::HangWatchState::GetHangWatchStateForCurrentThread();
  if (!state) {
    return;
  }

  // If a hang is currently being captured we should block here so execution
  // stops and we avoid recording unrelated stack frames in the crash.
  if (state->IsFlagSet(internal::HangWatchDeadline::Flag::kShouldBlockOnHang)) {
    base::HangWatcher::GetInstance()->BlockIfCaptureInProgress();
  }

#if DCHECK_IS_ON()
  // Verify that no Scope was destructed out of order.
  DCHECK_EQ(this, state->GetCurrentWatchHangsInScope());
  state->SetCurrentWatchHangsInScope(previous_watch_hangs_in_scope_);
#endif

  if (state->nesting_level() == 1) {
    // If a call to InvalidateActiveExpectations() suspended hang watching
    // during the lifetime of this or any nested WatchHangsInScope it can now
    // safely be reactivated by clearing the ignore bit since this is the
    // outer-most scope.
    state->UnsetIgnoreCurrentWatchHangsInScope();
  } else if (set_hangs_ignored_on_exit_) {
    // Return to ignoring hangs since this was the previous state before hang
    // watching was temporarily enabled for this WatchHangsInScope only in the
    // constructor.
    state->SetIgnoreCurrentWatchHangsInScope();
  }

  // Reset the deadline to the value it had before entering this
  // WatchHangsInScope.
  state->SetDeadline(previous_deadline_);
  // TODO(crbug.com/40111620): Log when a WatchHangsInScope exits after its
  // deadline and that went undetected by the HangWatcher.

  state->DecrementNestingLevel();
}

// static
void HangWatcher::InitializeOnMainThread(ProcessType process_type,
                                         bool emit_crashes) {
  DCHECK(!g_use_hang_watcher);
  DCHECK(g_io_thread_log_level == LoggingLevel::kNone);
  DCHECK(g_main_thread_log_level == LoggingLevel::kNone);
  DCHECK(g_threadpool_log_level == LoggingLevel::kNone);

  bool enable_hang_watcher = base::FeatureList::IsEnabled(kEnableHangWatcher);

  // Do not start HangWatcher in the GPU process until the issue related to
  // invalid magic signature in the GPU WatchDog is fixed
  // (https://crbug.com/1297760).
  if (process_type == ProcessType::kGPUProcess) {
    enable_hang_watcher = false;
  }

  g_use_hang_watcher.store(enable_hang_watcher, std::memory_order_relaxed);

  // Keep the process type.
  g_hang_watcher_process_type.store(process_type, std::memory_order_relaxed);

  // If hang watching is disabled as a whole there is no need to read the
  // params.
  if (!enable_hang_watcher) {
    return;
  }

  // Retrieve thread-specific config for hang watching.
  if (process_type == HangWatcher::ProcessType::kBrowserProcess) {
    // Crashes are set to always emit. Override any feature flags.
    if (emit_crashes) {
      g_io_thread_log_level.store(
          static_cast<LoggingLevel>(LoggingLevel::kUmaAndCrash),
          std::memory_order_relaxed);
      g_main_thread_log_level.store(
          static_cast<LoggingLevel>(LoggingLevel::kUmaAndCrash),
          std::memory_order_relaxed);
    } else {
      g_io_thread_log_level.store(
          static_cast<LoggingLevel>(kIOThreadLogLevel.Get()),
          std::memory_order_relaxed);
      g_main_thread_log_level.store(
          static_cast<LoggingLevel>(kUIThreadLogLevel.Get()),
          std::memory_order_relaxed);
    }

    g_threadpool_log_level.store(
        static_cast<LoggingLevel>(kThreadPoolLogLevel.Get()),
        std::memory_order_relaxed);
  } else if (process_type == HangWatcher::ProcessType::kGPUProcess) {
    g_threadpool_log_level.store(
        static_cast<LoggingLevel>(kGPUProcessThreadPoolLogLevel.Get()),
        std::memory_order_relaxed);
    g_io_thread_log_level.store(
        static_cast<LoggingLevel>(kGPUProcessIOThreadLogLevel.Get()),
        std::memory_order_relaxed);
    g_main_thread_log_level.store(
        static_cast<LoggingLevel>(kGPUProcessMainThreadLogLevel.Get()),
        std::memory_order_relaxed);
  } else if (process_type == HangWatcher::ProcessType::kRendererProcess) {
    g_threadpool_log_level.store(
        static_cast<LoggingLevel>(kRendererProcessThreadPoolLogLevel.Get()),
        std::memory_order_relaxed);
    g_io_thread_log_level.store(
        static_cast<LoggingLevel>(kRendererProcessIOThreadLogLevel.Get()),
        std::memory_order_relaxed);
    g_main_thread_log_level.store(
        static_cast<LoggingLevel>(kRendererProcessMainThreadLogLevel.Get()),
        std::memory_order_relaxed);
  } else if (process_type == HangWatcher::ProcessType::kUtilityProcess) {
    g_threadpool_log_level.store(
        static_cast<LoggingLevel>(kUtilityProcessThreadPoolLogLevel.Get()),
        std::memory_order_relaxed);
    g_io_thread_log_level.store(
        static_cast<LoggingLevel>(kUtilityProcessIOThreadLogLevel.Get()),
        std::memory_order_relaxed);
    g_main_thread_log_level.store(
        static_cast<LoggingLevel>(kUtilityProcessMainThreadLogLevel.Get()),
        std::memory_order_relaxed);
  }
}

void HangWatcher::UnitializeOnMainThreadForTesting() {
  g_use_hang_watcher.store(false, std::memory_order_relaxed);
  g_threadpool_log_level.store(LoggingLevel::kNone, std::memory_order_relaxed);
  g_io_thread_log_level.store(LoggingLevel::kNone, std::memory_order_relaxed);
  g_main_thread_log_level.store(LoggingLevel::kNone, std::memory_order_relaxed);
  g_shutting_down.store(false, std::memory_order_relaxed);
}

// static
bool HangWatcher::IsEnabled() {
  return g_use_hang_watcher.load(std::memory_order_relaxed);
}

// static
bool HangWatcher::IsThreadPoolHangWatchingEnabled() {
  return g_threadpool_log_level.load(std::memory_order_relaxed) !=
         LoggingLevel::kNone;
}

// static
bool HangWatcher::IsIOThreadHangWatchingEnabled() {
  return g_io_thread_log_level.load(std::memory_order_relaxed) !=
         LoggingLevel::kNone;
}

// static
bool HangWatcher::IsCrashReportingEnabled() {
  if (g_main_thread_log_level.load(std::memory_order_relaxed) ==
      LoggingLevel::kUmaAndCrash) {
    return true;
  }
  if (g_io_thread_log_level.load(std::memory_order_relaxed) ==
      LoggingLevel::kUmaAndCrash) {
    return true;
  }
  if (g_threadpool_log_level.load(std::memory_order_relaxed) ==
      LoggingLevel::kUmaAndCrash) {
    return true;
  }
  return false;
}

// static
void HangWatcher::InvalidateActiveExpectations() {
  auto* const state =
      internal::HangWatchState::GetHangWatchStateForCurrentThread();
  if (!state) {
    // If the current thread is not under watch there is nothing to invalidate.
    return;
  }
  state->SetIgnoreCurrentWatchHangsInScope();
}

// static
void HangWatcher::SetShuttingDown() {
  // memory_order_relaxed offers no memory order guarantees. In rare cases, we
  // could falsely log to BrowserProcess.Normal instead of
  // BrowserProcess.Shutdown. This is OK in practice.
  bool was_shutting_down =
      g_shutting_down.exchange(true, std::memory_order_relaxed);
  DCHECK(!was_shutting_down);
}

HangWatcher::HangWatcher()
    : monitoring_period_(kMonitoringPeriod),
      should_monitor_(WaitableEvent::ResetPolicy::AUTOMATIC),
      thread_(this, kThreadName),
      tick_clock_(base::DefaultTickClock::GetInstance()),
      memory_pressure_listener_(
          FROM_HERE,
          base::BindRepeating(&HangWatcher::OnMemoryPressure,
                              base::Unretained(this))) {
  // |thread_checker_| should not be bound to the constructing thread.
  DETACH_FROM_THREAD(hang_watcher_thread_checker_);

  should_monitor_.declare_only_used_while_idle();

  DCHECK(!g_instance);
  g_instance = this;
}

// static
void HangWatcher::CreateHangWatcherInstance() {
  DCHECK(!g_instance);
  g_instance = new base::HangWatcher();
  // The hang watcher is leaked to make sure it survives all watched threads.
  ANNOTATE_LEAKING_OBJECT_PTR(g_instance);
}

#if !BUILDFLAG(IS_NACL)
debug::ScopedCrashKeyString
HangWatcher::GetTimeSinceLastCriticalMemoryPressureCrashKey() {
  DCHECK_CALLED_ON_VALID_THREAD(hang_watcher_thread_checker_);

  // The crash key size is large enough to hold the biggest possible return
  // value from base::TimeDelta::InSeconds().
  constexpr debug::CrashKeySize kCrashKeyContentSize =
      debug::CrashKeySize::Size32;
  DCHECK_GE(static_cast<uint64_t>(kCrashKeyContentSize),
            base::NumberToString(std::numeric_limits<int64_t>::max()).size());

  static debug::CrashKeyString* crash_key = AllocateCrashKeyString(
      "seconds-since-last-memory-pressure", kCrashKeyContentSize);

  const base::TimeTicks last_critical_memory_pressure_time =
      last_critical_memory_pressure_.load(std::memory_order_relaxed);
  if (last_critical_memory_pressure_time.is_null()) {
    constexpr char kNoMemoryPressureMsg[] = "No critical memory pressure";
    static_assert(
        std::size(kNoMemoryPressureMsg) <=
            static_cast<uint64_t>(kCrashKeyContentSize),
        "The crash key is too small to hold \"No critical memory pressure\".");
    return debug::ScopedCrashKeyString(crash_key, kNoMemoryPressureMsg);
  } else {
    base::TimeDelta time_since_last_critical_memory_pressure =
        base::TimeTicks::Now() - last_critical_memory_pressure_time;
    return debug::ScopedCrashKeyString(
        crash_key, base::NumberToString(
                       time_since_last_critical_memory_pressure.InSeconds()));
  }
}
#endif

std::string HangWatcher::GetTimeSinceLastSystemPowerResumeCrashKeyValue()
    const {
  DCHECK_CALLED_ON_VALID_THREAD(hang_watcher_thread_checker_);

  const TimeTicks last_system_power_resume_time =
      PowerMonitor::GetInstance()->GetLastSystemResumeTime();
  if (last_system_power_resume_time.is_null()) {
    return "Never suspended";
  }
  if (last_system_power_resume_time == TimeTicks::Max()) {
    return "Power suspended";
  }

  const TimeDelta time_since_last_system_resume =
      TimeTicks::Now() - last_system_power_resume_time;
  return NumberToString(time_since_last_system_resume.InSeconds());
}

void HangWatcher::OnMemoryPressure(
    base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level) {
  if (memory_pressure_level ==
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL) {
    last_critical_memory_pressure_.store(base::TimeTicks::Now(),
                                         std::memory_order_relaxed);
  }
}

HangWatcher::~HangWatcher() {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  DCHECK_EQ(g_instance, this);
  DCHECK(watch_states_.empty());
  g_instance = nullptr;
  Stop();
}

void HangWatcher::Start() {
  thread_.Start();
  thread_started_ = true;
}

void HangWatcher::Stop() {
  g_keep_monitoring.store(false, std::memory_order_relaxed);
  should_monitor_.Signal();
  thread_.Join();
  thread_started_ = false;

  // In production HangWatcher is always leaked but during testing it's possibly
  // stopped and restarted using a new instance. This makes sure the next call
  // to Start() will actually monitor in that case.
  g_keep_monitoring.store(true, std::memory_order_relaxed);
}

bool HangWatcher::IsWatchListEmpty() {
  AutoLock auto_lock(watch_state_lock_);
  return watch_states_.empty();
}

void HangWatcher::Wait() {
  while (true) {
    // Amount by which the actual time spent sleeping can deviate from
    // the target time and still be considered timely.
    constexpr base::TimeDelta kWaitDriftTolerance = base::Milliseconds(100);

    const base::TimeTicks time_before_wait = tick_clock_->NowTicks();

    // Sleep until next scheduled monitoring or until signaled.
    const bool was_signaled = should_monitor_.TimedWait(monitoring_period_);

    if (after_wait_callback_) {
      after_wait_callback_.Run(time_before_wait);
    }

    const base::TimeTicks time_after_wait = tick_clock_->NowTicks();
    const base::TimeDelta wait_time = time_after_wait - time_before_wait;
    const bool wait_was_normal =
        wait_time <= (monitoring_period_ + kWaitDriftTolerance);

    if (!wait_was_normal) {
      // If the time spent waiting was too high it might indicate the machine is
      // very slow or that that it went to sleep. In any case we can't trust the
      // WatchHangsInScopes that are currently live. Update the ignore
      // threshold to make sure they don't trigger a hang on subsequent monitors
      // then keep waiting.

      base::AutoLock auto_lock(watch_state_lock_);

      // Find the latest deadline among the live watch states. They might change
      // atomically while iterating but that's fine because if they do that
      // means the new WatchHangsInScope was constructed very soon after the
      // abnormal sleep happened and might be affected by the root cause still.
      // Ignoring it is cautious and harmless.
      base::TimeTicks latest_deadline;
      for (const auto& state : watch_states_) {
        base::TimeTicks deadline = state->GetDeadline();
        if (deadline > latest_deadline) {
          latest_deadline = deadline;
        }
      }

      deadline_ignore_threshold_ = latest_deadline;
    }

    // Stop waiting.
    if (wait_was_normal || was_signaled) {
      return;
    }
  }
}

void HangWatcher::Run() {
  // Monitor() should only run on |thread_|. Bind |thread_checker_| here to make
  // sure of that.
  DCHECK_CALLED_ON_VALID_THREAD(hang_watcher_thread_checker_);

  while (g_keep_monitoring.load(std::memory_order_relaxed)) {
    Wait();

    if (!IsWatchListEmpty() &&
        g_keep_monitoring.load(std::memory_order_relaxed)) {
      Monitor();
      if (after_monitor_closure_for_testing_) {
        after_monitor_closure_for_testing_.Run();
      }
    }
  }
}

// static
HangWatcher* HangWatcher::GetInstance() {
  return g_instance;
}

// static
void HangWatcher::RecordHang() {
  base::debug::DumpWithoutCrashing();
  NO_CODE_FOLDING();
}

ScopedClosureRunner HangWatcher::RegisterThreadInternal(
    ThreadType thread_type) {
  AutoLock auto_lock(watch_state_lock_);
  CHECK(base::FeatureList::GetInstance());

  // Do not install a WatchState if the results would never be observable.
  if (!ThreadTypeLoggingLevelGreaterOrEqual(thread_type,
                                            LoggingLevel::kUmaOnly)) {
    return ScopedClosureRunner(base::DoNothing());
  }

  watch_states_.push_back(
      internal::HangWatchState::CreateHangWatchStateForCurrentThread(
          thread_type));
  return ScopedClosureRunner(BindOnce(&HangWatcher::UnregisterThread,
                                      Unretained(HangWatcher::GetInstance())));
}

// static
ScopedClosureRunner HangWatcher::RegisterThread(ThreadType thread_type) {
  if (!GetInstance()) {
    return ScopedClosureRunner();
  }

  return GetInstance()->RegisterThreadInternal(thread_type);
}

base::TimeTicks HangWatcher::WatchStateSnapShot::GetHighestDeadline() const {
  DCHECK(IsActionable());

  // Since entries are sorted in increasing order the last entry is the largest
  // one.
  return hung_watch_state_copies_.back().deadline;
}

HangWatcher::WatchStateSnapShot::WatchStateSnapShot() = default;

void HangWatcher::WatchStateSnapShot::Init(
    const HangWatchStates& watch_states,
    base::TimeTicks deadline_ignore_threshold,
    base::TimeDelta monitoring_period) {
  DCHECK(!initialized_);

  // No matter if the snapshot is actionable or not after this function
  // it will have been initialized.
  initialized_ = true;

  const base::TimeTicks now = base::TimeTicks::Now();
  bool all_threads_marked = true;
  bool found_deadline_before_ignore_threshold = false;

  // Use an std::array to store the hang counts to avoid allocations. The
  // numerical values of the HangWatcher::ThreadType enum is used to index into
  // the array. A |kInvalidHangCount| is used to signify there were no threads
  // of the type found.
  constexpr size_t kHangCountArraySize =
      static_cast<std::size_t>(base::HangWatcher::ThreadType::kMax) + 1;
  std::array<int, kHangCountArraySize> hung_counts_per_thread_type;

  constexpr int kInvalidHangCount = -1;
  hung_counts_per_thread_type.fill(kInvalidHangCount);

  // Will be true if any of the hung threads has a logging level high enough,
  // as defined through finch params, to warant dumping a crash.
  bool any_hung_thread_has_dumping_enabled = false;

  // Copy hung thread information.
  for (const auto& watch_state : watch_states) {
    uint64_t flags;
    TimeTicks deadline;
    std::tie(flags, deadline) = watch_state->GetFlagsAndDeadline();

    if (deadline <= deadline_ignore_threshold) {
      found_deadline_before_ignore_threshold = true;
    }

    if (internal::HangWatchDeadline::IsFlagSet(
            internal::HangWatchDeadline::Flag::kIgnoreCurrentWatchHangsInScope,
            flags)) {
      continue;
    }

    // If a thread type is monitored and did not hang it still needs to be
    // logged as a zero count;
    const size_t hang_count_index =
        static_cast<size_t>(watch_state.get()->thread_type());
    if (hung_counts_per_thread_type[hang_count_index] == kInvalidHangCount) {
      hung_counts_per_thread_type[hang_count_index] = 0;
    }

    // Only copy hung threads.
    if (deadline <= now) {
      ++hung_counts_per_thread_type[hang_count_index];

      if (ThreadTypeLoggingLevelGreaterOrEqual(watch_state.get()->thread_type(),
                                               LoggingLevel::kUmaAndCrash)) {
        any_hung_thread_has_dumping_enabled = true;
      }

#if BUILDFLAG(ENABLE_BASE_TRACING)
      // Emit trace events for monitored threads.
      if (ThreadTypeLoggingLevelGreaterOrEqual(watch_state.get()->thread_type(),
                                               LoggingLevel::kUmaOnly)) {
        const PlatformThreadId thread_id = watch_state.get()->GetThreadID();
        const auto track = perfetto::Track::FromPointer(
            this, perfetto::ThreadTrack::ForThread(thread_id));
        TRACE_EVENT_BEGIN("latency", "HangWatcher::ThreadHung", track,
                          deadline);
        TRACE_EVENT_END("latency", track, now);
      }
#endif

      // Attempt to mark the thread as needing to stay within its current
      // WatchHangsInScope until capture is complete.
      bool thread_marked = watch_state->SetShouldBlockOnHang(flags, deadline);

      // If marking some threads already failed the snapshot won't be kept so
      // there is no need to keep adding to it. The loop doesn't abort though
      // to keep marking the other threads. If these threads remain hung until
      // the next capture then they'll already be marked and will be included
      // in the capture at that time.
      if (thread_marked && all_threads_marked) {
        hung_watch_state_copies_.push_back(WatchStateCopy{
            deadline, watch_state.get()->GetSystemWideThreadID()});
      } else {
        all_threads_marked = false;
      }
    }
  }

  // Log the hung thread counts to histograms for each thread type if any thread
  // of the type were found.
  for (size_t i = 0; i < kHangCountArraySize; ++i) {
    const int hang_count = hung_counts_per_thread_type[i];
    const HangWatcher::ThreadType thread_type =
        static_cast<HangWatcher::ThreadType>(i);
    if (hang_count != kInvalidHangCount &&
        ThreadTypeLoggingLevelGreaterOrEqual(thread_type,
                                             LoggingLevel::kUmaOnly)) {
      LogStatusHistogram(thread_type, hang_count, now, monitoring_period);
    }
  }

  // Three cases can invalidate this snapshot and prevent the capture of the
  // hang.
  //
  // 1. Some threads could not be marked for blocking so this snapshot isn't
  // actionable since marked threads could be hung because of unmarked ones.
  // If only the marked threads were captured the information would be
  // incomplete.
  //
  // 2. Any of the threads have a deadline before |deadline_ignore_threshold|.
  // If any thread is ignored it reduces the confidence in the whole state and
  // it's better to avoid capturing misleading data.
  //
  // 3. The hung threads found were all of types that are not configured through
  // Finch to trigger a crash dump.
  //
  if (!all_threads_marked || found_deadline_before_ignore_threshold ||
      !any_hung_thread_has_dumping_enabled) {
    hung_watch_state_copies_.clear();
    return;
  }

  // Sort |hung_watch_state_copies_| by order of decreasing hang severity so the
  // most severe hang is first in the list.
  std::ranges::sort(hung_watch_state_copies_,
                    [](const WatchStateCopy& lhs, const WatchStateCopy& rhs) {
                      return lhs.deadline < rhs.deadline;
                    });
}

void HangWatcher::WatchStateSnapShot::Clear() {
  hung_watch_state_copies_.clear();
  initialized_ = false;
}

HangWatcher::WatchStateSnapShot::WatchStateSnapShot(
    const WatchStateSnapShot& other) = default;

HangWatcher::WatchStateSnapShot::~WatchStateSnapShot() = default;

std::string HangWatcher::WatchStateSnapShot::PrepareHungThreadListCrashKey()
    const {
  DCHECK(IsActionable());

  // Build a crash key string that contains the ids of the hung threads.
  constexpr char kSeparator{'|'};
  std::string list_of_hung_thread_ids;

  // Add as many thread ids to the crash key as possible.
  for (const WatchStateCopy& copy : hung_watch_state_copies_) {
    std::string fragment = base::NumberToString(copy.thread_id) + kSeparator;
    if (list_of_hung_thread_ids.size() + fragment.size() <
        static_cast<std::size_t>(debug::CrashKeySize::Size256)) {
      list_of_hung_thread_ids += fragment;
    } else {
      // Respect the by priority ordering of thread ids in the crash key by
      // stopping the construction as soon as one does not fit. This avoids
      // including lesser priority ids while omitting more important ones.
      break;
    }
  }

  return list_of_hung_thread_ids;
}

bool HangWatcher::WatchStateSnapShot::IsActionable() const {
  DCHECK(initialized_);
  return !hung_watch_state_copies_.empty();
}

HangWatcher::WatchStateSnapShot HangWatcher::GrabWatchStateSnapshotForTesting()
    const {
  WatchStateSnapShot snapshot;
  snapshot.Init(watch_states_, deadline_ignore_threshold_, TimeDelta());
  return snapshot;
}

void HangWatcher::Monitor() {
  DCHECK_CALLED_ON_VALID_THREAD(hang_watcher_thread_checker_);
  AutoLock auto_lock(watch_state_lock_);

  // If all threads unregistered since this function was invoked there's
  // nothing to do anymore.
  if (watch_states_.empty()) {
    return;
  }

  watch_state_snapshot_.Init(watch_states_, deadline_ignore_threshold_,
                             monitoring_period_);

  if (watch_state_snapshot_.IsActionable()) {
    DoDumpWithoutCrashing(watch_state_snapshot_);
  }

  watch_state_snapshot_.Clear();
}

void HangWatcher::DoDumpWithoutCrashing(
    const WatchStateSnapShot& watch_state_snapshot) {
  TRACE_EVENT("latency", "HangWatcher::DoDumpWithoutCrashing");

  capture_in_progress_.store(true, std::memory_order_relaxed);
  base::AutoLock scope_lock(capture_lock_);

#if !BUILDFLAG(IS_NACL)
  const std::string list_of_hung_thread_ids =
      watch_state_snapshot.PrepareHungThreadListCrashKey();

  static debug::CrashKeyString* crash_key = AllocateCrashKeyString(
      "list-of-hung-threads", debug::CrashKeySize::Size256);

  const debug::ScopedCrashKeyString list_of_hung_threads_crash_key_string(
      crash_key, list_of_hung_thread_ids);

  const debug::ScopedCrashKeyString
      time_since_last_critical_memory_pressure_crash_key_string =
          GetTimeSinceLastCriticalMemoryPressureCrashKey();

  SCOPED_CRASH_KEY_STRING32("HangWatcher", "seconds-since-last-resume",
                            GetTimeSinceLastSystemPowerResumeCrashKeyValue());

  SCOPED_CRASH_KEY_BOOL("HangWatcher", "shutting-down",
                        g_shutting_down.load(std::memory_order_relaxed));
#endif

  // To avoid capturing more than one hang that blames a subset of the same
  // threads it's necessary to keep track of what is the furthest deadline
  // that contributed to declaring a hang. Only once
  // all threads have deadlines past this point can we be sure that a newly
  // discovered hang is not directly related.
  // Example:
  // **********************************************************************
  // Timeline A : L------1-------2----------3-------4----------N-----------
  // Timeline B : -------2----------3-------4----------L----5------N-------
  // Timeline C : L----------------------------5------6----7---8------9---N
  // **********************************************************************
  // In the example when a Monitor() happens during timeline A
  // |deadline_ignore_threshold_| (L) is at time zero and deadlines (1-4)
  // are before Now() (N) . A hang is captured and L is updated. During
  // the next Monitor() (timeline B) a new deadline is over but we can't
  // capture a hang because deadlines 2-4 are still live and already counted
  // toward a hang. During a third monitor (timeline C) all live deadlines
  // are now after L and a second hang can be recorded.
  base::TimeTicks latest_expired_deadline =
      watch_state_snapshot.GetHighestDeadline();

  if (on_hang_closure_for_testing_) {
    on_hang_closure_for_testing_.Run();
  } else {
    RecordHang();
  }

  // Update after running the actual capture.
  deadline_ignore_threshold_ = latest_expired_deadline;

  capture_in_progress_.store(false, std::memory_order_relaxed);
}

void HangWatcher::SetAfterMonitorClosureForTesting(
    base::RepeatingClosure closure) {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  after_monitor_closure_for_testing_ = std::move(closure);
}

void HangWatcher::SetOnHangClosureForTesting(base::RepeatingClosure closure) {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  on_hang_closure_for_testing_ = std::move(closure);
}

void HangWatcher::SetMonitoringPeriodForTesting(base::TimeDelta period) {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  monitoring_period_ = period;
}

void HangWatcher::SetAfterWaitCallbackForTesting(
    RepeatingCallback<void(TimeTicks)> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  after_wait_callback_ = callback;
}

void HangWatcher::SignalMonitorEventForTesting() {
  DCHECK_CALLED_ON_VALID_THREAD(constructing_thread_checker_);
  should_monitor_.Signal();
}

// static
void HangWatcher::StopMonitoringForTesting() {
  g_keep_monitoring.store(false, std::memory_order_relaxed);
}

void HangWatcher::SetTickClockForTesting(const base::TickClock* tick_clock) {
  tick_clock_ = tick_clock;
}

void HangWatcher::BlockIfCaptureInProgress() {
  // Makes a best-effort attempt to block execution if a hang is currently being
  // captured. Only block on |capture_lock| if |capture_in_progress_| hints that
  // it's already held to avoid serializing all threads on this function when no
  // hang capture is in-progress.
  if (capture_in_progress_.load(std::memory_order_relaxed)) {
    base::AutoLock hang_lock(capture_lock_);
  }
}

void HangWatcher::UnregisterThread() {
  AutoLock auto_lock(watch_state_lock_);

  auto it = std::ranges::find(
      watch_states_,
      internal::HangWatchState::GetHangWatchStateForCurrentThread(),
      &std::unique_ptr<internal::HangWatchState>::get);

  // Thread should be registered to get unregistered.
  CHECK(it != watch_states_.end(), base::NotFatalUntil::M125);

  watch_states_.erase(it);
}

namespace internal {
namespace {

constexpr uint64_t kOnlyDeadlineMask = 0x00FF'FFFF'FFFF'FFFFu;
constexpr uint64_t kOnlyFlagsMask = ~kOnlyDeadlineMask;
constexpr uint64_t kMaximumFlag = 0x8000'0000'0000'0000u;

// Use as a mask to keep persistent flags and the deadline.
constexpr uint64_t kPersistentFlagsAndDeadlineMask =
    kOnlyDeadlineMask |
    static_cast<uint64_t>(
        HangWatchDeadline::Flag::kIgnoreCurrentWatchHangsInScope);
}  // namespace

// Flag binary representation assertions.
static_assert(
    static_cast<uint64_t>(HangWatchDeadline::Flag::kMinValue) >
        kOnlyDeadlineMask,
    "Invalid numerical value for flag. Would interfere with bits of data.");
static_assert(static_cast<uint64_t>(HangWatchDeadline::Flag::kMaxValue) <=
                  kMaximumFlag,
              "A flag can only set a single bit.");

HangWatchDeadline::HangWatchDeadline() = default;
HangWatchDeadline::~HangWatchDeadline() = default;

std::pair<uint64_t, TimeTicks> HangWatchDeadline::GetFlagsAndDeadline() const {
  uint64_t bits = bits_.load(std::memory_order_relaxed);
  return std::make_pair(ExtractFlags(bits),
                        DeadlineFromBits(ExtractDeadline((bits))));
}

TimeTicks HangWatchDeadline::GetDeadline() const {
  return DeadlineFromBits(
      ExtractDeadline(bits_.load(std::memory_order_relaxed)));
}

// static
TimeTicks HangWatchDeadline::Max() {
  // |kOnlyDeadlineMask| has all the bits reserved for the TimeTicks value
  // set. This means it also represents the highest representable value.
  return DeadlineFromBits(kOnlyDeadlineMask);
}

// static
bool HangWatchDeadline::IsFlagSet(Flag flag, uint64_t flags) {
  return static_cast<uint64_t>(flag) & flags;
}

void HangWatchDeadline::SetDeadline(TimeTicks new_deadline) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(new_deadline <= Max()) << "Value too high to be represented.";
  DCHECK(new_deadline >= TimeTicks{}) << "Value cannot be negative.";

  if (switch_bits_callback_for_testing_) {
    const uint64_t switched_in_bits = SwitchBitsForTesting();
    // If a concurrent deadline change is tested it cannot have a deadline or
    // persistent flag change since those always happen on the same thread.
    DCHECK((switched_in_bits & kPersistentFlagsAndDeadlineMask) == 0u);
  }

  // Discard all non-persistent flags and apply deadline change.
  const uint64_t old_bits = bits_.load(std::memory_order_relaxed);
  const uint64_t new_flags =
      ExtractFlags(old_bits & kPersistentFlagsAndDeadlineMask);
  bits_.store(new_flags | ExtractDeadline(static_cast<uint64_t>(
                              new_deadline.ToInternalValue())),
              std::memory_order_relaxed);
}

// TODO(crbug.com/40132796): Add flag DCHECKs here.
bool HangWatchDeadline::SetShouldBlockOnHang(uint64_t old_flags,
                                             TimeTicks old_deadline) {
  DCHECK(old_deadline <= Max()) << "Value too high to be represented.";
  DCHECK(old_deadline >= TimeTicks{}) << "Value cannot be negative.";

  // Set the kShouldBlockOnHang flag only if |bits_| did not change since it was
  // read. kShouldBlockOnHang is the only non-persistent flag and should never
  // be set twice. Persistent flags and deadline changes are done from the same
  // thread so there is no risk of losing concurrently added information.
  uint64_t old_bits =
      old_flags | static_cast<uint64_t>(old_deadline.ToInternalValue());
  const uint64_t desired_bits =
      old_bits | static_cast<uint64_t>(Flag::kShouldBlockOnHang);

  // If a test needs to simulate |bits_| changing since calling this function
  // this happens now.
  if (switch_bits_callback_for_testing_) {
    const uint64_t switched_in_bits = SwitchBitsForTesting();

    // Injecting the flag being tested is invalid.
    DCHECK(!IsFlagSet(Flag::kShouldBlockOnHang, switched_in_bits));
  }

  return bits_.compare_exchange_weak(old_bits, desired_bits,
                                     std::memory_order_relaxed,
                                     std::memory_order_relaxed);
}

void HangWatchDeadline::SetIgnoreCurrentWatchHangsInScope() {
  SetPersistentFlag(Flag::kIgnoreCurrentWatchHangsInScope);
}

void HangWatchDeadline::UnsetIgnoreCurrentWatchHangsInScope() {
  ClearPersistentFlag(Flag::kIgnoreCurrentWatchHangsInScope);
}

void HangWatchDeadline::SetPersistentFlag(Flag flag) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (switch_bits_callback_for_testing_) {
    SwitchBitsForTesting();
  }
  bits_.fetch_or(static_cast<uint64_t>(flag), std::memory_order_relaxed);
}

void HangWatchDeadline::ClearPersistentFlag(Flag flag) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (switch_bits_callback_for_testing_) {
    SwitchBitsForTesting();
  }
  bits_.fetch_and(~(static_cast<uint64_t>(flag)), std::memory_order_relaxed);
}

// static
uint64_t HangWatchDeadline::ExtractFlags(uint64_t bits) {
  return bits & kOnlyFlagsMask;
}

// static
uint64_t HangWatchDeadline::ExtractDeadline(uint64_t bits) {
  return bits & kOnlyDeadlineMask;
}

// static
TimeTicks HangWatchDeadline::DeadlineFromBits(uint64_t bits) {
  // |kOnlyDeadlineMask| has all the deadline bits set to 1 so is the largest
  // representable value.
  DCHECK(bits <= kOnlyDeadlineMask)
      << "Flags bits are set. Remove them before returning deadline.";
  static_assert(kOnlyDeadlineMask <= std::numeric_limits<int64_t>::max());
  return TimeTicks::FromInternalValue(static_cast<int64_t>(bits));
}

bool HangWatchDeadline::IsFlagSet(Flag flag) const {
  return bits_.load(std::memory_order_relaxed) & static_cast<uint64_t>(flag);
}

void HangWatchDeadline::SetSwitchBitsClosureForTesting(
    RepeatingCallback<uint64_t(void)> closure) {
  switch_bits_callback_for_testing_ = closure;
}

void HangWatchDeadline::ResetSwitchBitsClosureForTesting() {
  DCHECK(switch_bits_callback_for_testing_);
  switch_bits_callback_for_testing_.Reset();
}

uint64_t HangWatchDeadline::SwitchBitsForTesting() {
  DCHECK(switch_bits_callback_for_testing_);

  const uint64_t old_bits = bits_.load(std::memory_order_relaxed);
  const uint64_t new_bits = switch_bits_callback_for_testing_.Run();
  const uint64_t old_flags = ExtractFlags(old_bits);

  const uint64_t switched_in_bits = old_flags | new_bits;
  bits_.store(switched_in_bits, std::memory_order_relaxed);
  return switched_in_bits;
}

HangWatchState::HangWatchState(HangWatcher::ThreadType thread_type)
    : resetter_(&hang_watch_state, this, nullptr), thread_type_(thread_type) {
#if BUILDFLAG(IS_MAC)
  pthread_threadid_np(pthread_self(), &system_wide_thread_id_);
#endif
  thread_id_ = PlatformThread::CurrentId();
}

HangWatchState::~HangWatchState() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  DCHECK_EQ(GetHangWatchStateForCurrentThread(), this);

#if DCHECK_IS_ON()
  // Destroying the HangWatchState should not be done if there are live
  // WatchHangsInScopes.
  DCHECK(!current_watch_hangs_in_scope_);
#endif
}

// static
std::unique_ptr<HangWatchState>
HangWatchState::CreateHangWatchStateForCurrentThread(
    HangWatcher::ThreadType thread_type) {
  // Allocate a watch state object for this thread.
  std::unique_ptr<HangWatchState> hang_state =
      std::make_unique<HangWatchState>(thread_type);

  // Setting the thread local worked.
  DCHECK_EQ(GetHangWatchStateForCurrentThread(), hang_state.get());

  // Transfer ownership to caller.
  return hang_state;
}

TimeTicks HangWatchState::GetDeadline() const {
  return deadline_.GetDeadline();
}

std::pair<uint64_t, TimeTicks> HangWatchState::GetFlagsAndDeadline() const {
  return deadline_.GetFlagsAndDeadline();
}

void HangWatchState::SetDeadline(TimeTicks deadline) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  deadline_.SetDeadline(deadline);
}

bool HangWatchState::IsOverDeadline() const {
  return TimeTicks::Now() > deadline_.GetDeadline();
}

void HangWatchState::SetIgnoreCurrentWatchHangsInScope() {
  deadline_.SetIgnoreCurrentWatchHangsInScope();
}

void HangWatchState::UnsetIgnoreCurrentWatchHangsInScope() {
  deadline_.UnsetIgnoreCurrentWatchHangsInScope();
}

bool HangWatchState::SetShouldBlockOnHang(uint64_t old_flags,
                                          TimeTicks old_deadline) {
  return deadline_.SetShouldBlockOnHang(old_flags, old_deadline);
}

bool HangWatchState::IsFlagSet(HangWatchDeadline::Flag flag) {
  return deadline_.IsFlagSet(flag);
}

#if DCHECK_IS_ON()
void HangWatchState::SetCurrentWatchHangsInScope(
    WatchHangsInScope* current_hang_watch_scope_enable) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  current_watch_hangs_in_scope_ = current_hang_watch_scope_enable;
}

WatchHangsInScope* HangWatchState::GetCurrentWatchHangsInScope() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return current_watch_hangs_in_scope_;
}
#endif

HangWatchDeadline* HangWatchState::GetHangWatchDeadlineForTesting() {
  return &deadline_;
}

void HangWatchState::IncrementNestingLevel() {
  ++nesting_level_;
}

void HangWatchState::DecrementNestingLevel() {
  --nesting_level_;
}

// static
HangWatchState* HangWatchState::GetHangWatchStateForCurrentThread() {
  // Workaround false-positive MSAN use-of-uninitialized-value on
  // thread_local storage for loaded libraries:
  // https://github.com/google/sanitizers/issues/1265
  MSAN_UNPOISON(&hang_watch_state, sizeof(internal::HangWatchState*));

  return hang_watch_state;
}

PlatformThreadId HangWatchState::GetThreadID() const {
  return thread_id_;
}

uint64_t HangWatchState::GetSystemWideThreadID() const {
#if BUILDFLAG(IS_MAC)
  return system_wide_thread_id_;
#else
  CHECK(thread_id_ > 0);
  return static_cast<uint64_t>(thread_id_);
#endif
}

}  // namespace internal

}  // namespace base
