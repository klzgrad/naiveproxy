// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

#include <utility>

#include "base/logging.h"
#include "base/values.h"
#include "build/build_config.h"

#if defined(OS_MACOSX) || defined(OS_LINUX) || defined(OS_AIX)
namespace {
int CalculateEventsPerSecond(uint64_t event_count,
                             uint64_t* last_event_count,
                             base::TimeTicks* last_calculated) {
  base::TimeTicks time = base::TimeTicks::Now();

  if (*last_event_count == 0) {
    // First call, just set the last values.
    *last_calculated = time;
    *last_event_count = event_count;
    return 0;
  }

  int64_t events_delta = event_count - *last_event_count;
  int64_t time_delta = (time - *last_calculated).InMicroseconds();
  if (time_delta == 0) {
    NOTREACHED();
    return 0;
  }

  *last_calculated = time;
  *last_event_count = event_count;

  int64_t events_delta_for_ms =
      events_delta * base::Time::kMicrosecondsPerSecond;
  // Round the result up by adding 1/2 (the second term resolves to 1/2 without
  // dropping down into floating point).
  return (events_delta_for_ms + time_delta / 2) / time_delta;
}

}  // namespace
#endif  // defined(OS_MACOSX) || defined(OS_LINUX) || defined(OS_AIX)

namespace base {

SystemMemoryInfoKB::SystemMemoryInfoKB() = default;

SystemMemoryInfoKB::SystemMemoryInfoKB(const SystemMemoryInfoKB& other) =
    default;

SystemMetrics::SystemMetrics() {
  committed_memory_ = 0;
}

SystemMetrics SystemMetrics::Sample() {
  SystemMetrics system_metrics;

  system_metrics.committed_memory_ = GetSystemCommitCharge();
#if defined(OS_LINUX) || defined(OS_ANDROID)
  GetSystemMemoryInfo(&system_metrics.memory_info_);
  GetVmStatInfo(&system_metrics.vmstat_info_);
  GetSystemDiskInfo(&system_metrics.disk_info_);
#endif
#if defined(OS_CHROMEOS)
  GetSwapInfo(&system_metrics.swap_info_);
#endif

  return system_metrics;
}

std::unique_ptr<Value> SystemMetrics::ToValue() const {
  std::unique_ptr<DictionaryValue> res(new DictionaryValue());

  res->SetInteger("committed_memory", static_cast<int>(committed_memory_));
#if defined(OS_LINUX) || defined(OS_ANDROID)
  std::unique_ptr<DictionaryValue> meminfo = memory_info_.ToValue();
  std::unique_ptr<DictionaryValue> vmstat = vmstat_info_.ToValue();
  meminfo->MergeDictionary(vmstat.get());
  res->Set("meminfo", std::move(meminfo));
  res->Set("diskinfo", disk_info_.ToValue());
#endif
#if defined(OS_CHROMEOS)
  res->Set("swapinfo", swap_info_.ToValue());
#endif

  return std::move(res);
}

std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateCurrentProcessMetrics() {
#if !defined(OS_MACOSX) || defined(OS_IOS)
  return CreateProcessMetrics(base::GetCurrentProcessHandle());
#else
  return CreateProcessMetrics(base::GetCurrentProcessHandle(), nullptr);
#endif  // !defined(OS_MACOSX) || defined(OS_IOS)
}

#if defined(OS_MACOSX) || defined(OS_LINUX) || defined(OS_AIX)
int ProcessMetrics::CalculateIdleWakeupsPerSecond(
    uint64_t absolute_idle_wakeups) {
  return CalculateEventsPerSecond(absolute_idle_wakeups,
                                  &last_absolute_idle_wakeups_,
                                  &last_idle_wakeups_time_);
}
#else
int ProcessMetrics::GetIdleWakeupsPerSecond() {
  NOTIMPLEMENTED();  // http://crbug.com/120488
  return 0;
}
#endif  // defined(OS_MACOSX) || defined(OS_LINUX) || defined(OS_AIX)

#if defined(OS_MACOSX)
int ProcessMetrics::CalculatePackageIdleWakeupsPerSecond(
    uint64_t absolute_package_idle_wakeups) {
  return CalculateEventsPerSecond(absolute_package_idle_wakeups,
                                  &last_absolute_package_idle_wakeups_,
                                  &last_package_idle_wakeups_time_);
}

#endif  // defined(OS_MACOSX)
}  // namespace base
