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

#include <atomic>

#include "perfetto/base/time.h"

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace perfetto {
namespace base {

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#if !PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)
namespace {

// Returns the current value of the performance counter.
int64_t QPCNowRaw() {
  LARGE_INTEGER perf_counter_now = {};
  // According to the MSDN documentation for QueryPerformanceCounter(), this
  // will never fail on systems that run XP or later.
  // https://msdn.microsoft.com/library/windows/desktop/ms644904.aspx
  ::QueryPerformanceCounter(&perf_counter_now);
  return perf_counter_now.QuadPart;
}

double TSCTicksPerSecond() {
  // The value returned by QueryPerformanceFrequency() cannot be used as the TSC
  // frequency, because there is no guarantee that the TSC frequency is equal to
  // the performance counter frequency.
  // The TSC frequency is cached in a static variable because it takes some time
  // to compute it.
  static std::atomic<double> tsc_ticks_per_second = 0;
  double value = tsc_ticks_per_second.load(std::memory_order_relaxed);
  if (value != 0)
    return value;

  // Increase the thread priority to reduces the chances of having a context
  // switch during a reading of the TSC and the performance counter.
  const int previous_priority = ::GetThreadPriority(::GetCurrentThread());
  ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

  // The first time that this function is called, make an initial reading of the
  // TSC and the performance counter. Initialization of static variable is
  // thread-safe. Threads can race initializing tsc_initial vs
  // perf_counter_initial, although they should be storing very similar values.

  static const uint64_t tsc_initial = __rdtsc();
  static const int64_t perf_counter_initial = QPCNowRaw();

  // Make a another reading of the TSC and the performance counter every time
  // that this function is called.
  const uint64_t tsc_now = __rdtsc();
  const int64_t perf_counter_now = QPCNowRaw();

  // Reset the thread priority.
  ::SetThreadPriority(::GetCurrentThread(), previous_priority);

  // Make sure that at least 50 ms elapsed between the 2 readings. The first
  // time that this function is called, we don't expect this to be the case.
  // Note: The longer the elapsed time between the 2 readings is, the more
  //   accurate the computed TSC frequency will be. The 50 ms value was
  //   chosen because local benchmarks show that it allows us to get a
  //   stddev of less than 1 tick/us between multiple runs.
  // Note: According to the MSDN documentation for QueryPerformanceFrequency(),
  //   this will never fail on systems that run XP or later.
  //   https://msdn.microsoft.com/library/windows/desktop/ms644905.aspx
  LARGE_INTEGER perf_counter_frequency = {};
  ::QueryPerformanceFrequency(&perf_counter_frequency);
  PERFETTO_CHECK(perf_counter_now >= perf_counter_initial);
  const int64_t perf_counter_ticks = perf_counter_now - perf_counter_initial;
  const double elapsed_time_seconds =
      static_cast<double>(perf_counter_ticks) /
      static_cast<double>(perf_counter_frequency.QuadPart);

  constexpr double kMinimumEvaluationPeriodSeconds = 0.05;
  if (elapsed_time_seconds < kMinimumEvaluationPeriodSeconds)
    return 0;

  // Compute the frequency of the TSC.
  PERFETTO_CHECK(tsc_now >= tsc_initial);
  const uint64_t tsc_ticks = tsc_now - tsc_initial;
  // Racing with another thread to write |tsc_ticks_per_second| is benign
  // because both threads will write a valid result.
  tsc_ticks_per_second.store(
      static_cast<double>(tsc_ticks) / elapsed_time_seconds,
      std::memory_order_relaxed);

  return tsc_ticks_per_second.load(std::memory_order_relaxed);
}

}  // namespace
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)

TimeNanos GetWallTimeNs() {
  LARGE_INTEGER freq;
  ::QueryPerformanceFrequency(&freq);
  LARGE_INTEGER counter;
  ::QueryPerformanceCounter(&counter);
  double elapsed_nanoseconds = (1e9 * static_cast<double>(counter.QuadPart)) /
                               static_cast<double>(freq.QuadPart);
  return TimeNanos(static_cast<uint64_t>(elapsed_nanoseconds));
}

TimeNanos GetThreadCPUTimeNs() {
#if PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)
  // QueryThreadCycleTime versus TSCTicksPerSecond doesn't have much relation to
  // actual elapsed time on Windows on Arm, because QueryThreadCycleTime is
  // backed by the actual number of CPU cycles executed, rather than a
  // constant-rate timer like Intel. To work around this, use GetThreadTimes
  // (which isn't as accurate but is meaningful as a measure of elapsed
  // per-thread time).
  FILETIME dummy, kernel_ftime, user_ftime;
  ::GetThreadTimes(GetCurrentThread(), &dummy, &dummy, &kernel_ftime,
                   &user_ftime);
  uint64_t kernel_time =
      kernel_ftime.dwHighDateTime * 0x100000000 + kernel_ftime.dwLowDateTime;
  uint64_t user_time =
      user_ftime.dwHighDateTime * 0x100000000 + user_ftime.dwLowDateTime;

  return TimeNanos((kernel_time + user_time) * 100);
#else   // !PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)
  // Get the number of TSC ticks used by the current thread.
  ULONG64 thread_cycle_time = 0;
  ::QueryThreadCycleTime(GetCurrentThread(), &thread_cycle_time);

  // Get the frequency of the TSC.
  const double tsc_ticks_per_second = TSCTicksPerSecond();
  if (tsc_ticks_per_second == 0)
    return TimeNanos();

  // Return the CPU time of the current thread.
  const double thread_time_seconds =
      static_cast<double>(thread_cycle_time) / tsc_ticks_per_second;
  constexpr int64_t kNanosecondsPerSecond = 1000 * 1000 * 1000;
  return TimeNanos(
      static_cast<int64_t>(thread_time_seconds * kNanosecondsPerSecond));
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)
}

void SleepMicroseconds(unsigned interval_us) {
  // The Windows Sleep function takes a millisecond count. Round up so that
  // short sleeps don't turn into a busy wait. Note that the sleep granularity
  // on Windows can dynamically vary from 1 ms to ~16 ms, so don't count on this
  // being a short sleep.
  ::Sleep(static_cast<DWORD>((interval_us + 999) / 1000));
}

void InitializeTime() {
#if !PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)
  // Make an early first call to TSCTicksPerSecond() to start 50 ms elapsed time
  // (see comment in TSCTicksPerSecond()).
  TSCTicksPerSecond();
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)
}

#else  // PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

void SleepMicroseconds(unsigned interval_us) {
  ::usleep(static_cast<useconds_t>(interval_us));
}

void InitializeTime() {}

#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

std::string GetTimeFmt(const std::string& fmt) {
  time_t raw_time;
  time(&raw_time);
  struct tm local_tm;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  PERFETTO_CHECK(localtime_s(&local_tm, &raw_time) == 0);
#else
  tzset();
  PERFETTO_CHECK(localtime_r(&raw_time, &local_tm) != nullptr);
#endif
  char buf[128];
  PERFETTO_CHECK(strftime(buf, 80, fmt.c_str(), &local_tm) > 0);
  return buf;
}

std::optional<int32_t> GetTimezoneOffsetMins() {
  std::string tz = GetTimeFmt("%z");
  if (tz.size() != 5 || (tz[0] != '+' && tz[0] != '-'))
    return std::nullopt;
  char sign = '\0';
  int32_t hh = 0;
  int32_t mm = 0;
  if (sscanf(tz.c_str(), "%c%2d%2d", &sign, &hh, &mm) != 3)
    return std::nullopt;
  return (hh * 60 + mm) * (sign == '-' ? -1 : 1);
}

}  // namespace base
}  // namespace perfetto
