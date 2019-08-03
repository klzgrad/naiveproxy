// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "base/memory/memory_pressure_monitor_notifying_chromeos.h"

#include <fcntl.h>
#include <sys/poll.h>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/no_destructor.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/process_metrics.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/task/post_task.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"

namespace base {
namespace chromeos {

namespace {
// Type-safe version of |g_monitor_notifying| from
// base/memory/memory_pressure_monitor.cc, this was originally added because
// TabManagerDelegate for chromeos needs to call into ScheduleEarlyCheck which
// isn't a public API in the base MemoryPressureMonitor. This matters because
// ChromeOS may create a FakeMemoryPressureMonitor for browser tests and that's
// why this type-specific version was added.
MemoryPressureMonitorNotifying* g_monitor_notifying = nullptr;

// We try not to re-notify on moderate too frequently, this time
// controls how frequently we will notify after our first notification.
constexpr base::TimeDelta kModerateMemoryPressureCooldownTime =
    base::TimeDelta::FromSeconds(10);

// The margin mem file contains the two memory levels, the first is the
// critical level and the second is the moderate level. Note, this
// file may contain more values but only the first two are used for
// memory pressure notifications in chromeos.
constexpr char kMarginMemFile[] = "/sys/kernel/mm/chromeos-low_mem/margin";

// The available memory file contains the available memory as determined
// by the kernel.
constexpr char kAvailableMemFile[] =
    "/sys/kernel/mm/chromeos-low_mem/available";

// Converts an available memory value in MB to a memory pressure level.
MemoryPressureListener::MemoryPressureLevel GetMemoryPressureLevelFromAvailable(
    int available_mb,
    int moderate_avail_mb,
    int critical_avail_mb) {
  if (available_mb < critical_avail_mb)
    return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL;
  if (available_mb < moderate_avail_mb)
    return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE;

  return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE;
}

int64_t ReadAvailableMemoryMB(int available_fd) {
  // Read the available memory.
  char buf[32] = {};

  // kernfs/file.c:
  // "Once poll/select indicates that the value has changed, you
  // need to close and re-open the file, or seek to 0 and read again.
  ssize_t bytes_read = HANDLE_EINTR(pread(available_fd, buf, sizeof(buf), 0));
  PCHECK(bytes_read != -1);

  std::string mem_str(buf, bytes_read);
  int64_t available = -1;
  CHECK(base::StringToInt64(
      base::TrimWhitespaceASCII(mem_str, base::TrimPositions::TRIM_ALL),
      &available));

  return available;
}

// This function will wait until the /sys/kernel/mm/chromeos-low_mem/available
// file becomes readable and then read the latest value. This file will only
// become readable once the available memory cross through one of the margin
// values specified in /sys/kernel/mm/chromeos-low_mem/margin, for more
// details see https://crrev.com/c/536336.
bool WaitForMemoryPressureChanges(int available_fd) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::WILL_BLOCK);

  pollfd pfd = {available_fd, POLLPRI | POLLERR, 0};
  int res = HANDLE_EINTR(poll(&pfd, 1, -1));  // Wait indefinitely.
  PCHECK(res != -1);

  if (pfd.revents != (POLLPRI | POLLERR)) {
    // If we didn't receive POLLPRI | POLLERR it means we likely received
    // POLLNVAL because the fd has been closed.
    LOG(ERROR) << "WaitForMemoryPressureChanges received unexpected revents: "
               << pfd.revents;

    // We no longer want to wait for a kernel notification if the fd has been
    // closed.
    return false;
  }

  return true;
}

}  // namespace

MemoryPressureMonitorNotifying::MemoryPressureMonitorNotifying()
    : MemoryPressureMonitorNotifying(
          kMarginMemFile,
          kAvailableMemFile,
          base::BindRepeating(&WaitForMemoryPressureChanges),
          /*enable_metrics=*/true) {}

MemoryPressureMonitorNotifying::MemoryPressureMonitorNotifying(
    const std::string& margin_file,
    const std::string& available_file,
    base::RepeatingCallback<bool(int)> kernel_waiting_callback,
    bool enable_metrics)
    : available_mem_file_(HANDLE_EINTR(open(available_file.c_str(), O_RDONLY))),
      dispatch_callback_(
          base::BindRepeating(&MemoryPressureListener::NotifyMemoryPressure)),
      kernel_waiting_callback_(
          base::BindRepeating(std::move(kernel_waiting_callback),
                              available_mem_file_.get())),
      weak_ptr_factory_(this) {
  DCHECK(g_monitor_notifying == nullptr);
  g_monitor_notifying = this;

  CHECK(available_mem_file_.is_valid());
  std::vector<int> margin_parts =
      MemoryPressureMonitorNotifying::GetMarginFileParts(margin_file);

  // This class SHOULD have verified kernel support by calling
  // SupportsKernelNotifications() before creating a new instance of this.
  // Therefore we will check fail if we don't have multiple margin values.
  CHECK_LE(2u, margin_parts.size());
  critical_pressure_threshold_mb_ = margin_parts[0];
  moderate_pressure_threshold_mb_ = margin_parts[1];

  if (enable_metrics) {
    // We will report the current memory pressure at some periodic interval,
    // the metric ChromeOS.MemoryPRessureLevel is currently reported every 1s.
    reporting_timer_.Start(
        FROM_HERE, base::TimeDelta::FromSeconds(1),
        base::BindRepeating(&MemoryPressureMonitorNotifying::
                                CheckMemoryPressureAndRecordStatistics,
                            weak_ptr_factory_.GetWeakPtr()));
  }

  ScheduleWaitForKernelNotification();
}

MemoryPressureMonitorNotifying::~MemoryPressureMonitorNotifying() {
  DCHECK(g_monitor_notifying);
  g_monitor_notifying = nullptr;
}

std::vector<int> MemoryPressureMonitorNotifying::GetMarginFileParts() {
  static const base::NoDestructor<std::vector<int>> margin_file_parts(
      GetMarginFileParts(kMarginMemFile));
  return *margin_file_parts;
}

std::vector<int> MemoryPressureMonitorNotifying::GetMarginFileParts(
    const std::string& file) {
  std::vector<int> margin_values;
  std::string margin_contents;
  if (base::ReadFileToString(base::FilePath(file), &margin_contents)) {
    std::vector<std::string> margins =
        base::SplitString(margin_contents, base::kWhitespaceASCII,
                          base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    for (const auto& v : margins) {
      int value = -1;
      if (!base::StringToInt(v, &value)) {
        // If any of the values weren't parseable as an int we return
        // nothing as the file format is unexpected.
        LOG(ERROR) << "Unable to parse margin file contents as integer: " << v;
        return std::vector<int>();
      }
      margin_values.push_back(value);
    }
  } else {
    LOG(ERROR) << "Unable to read margin file: " << kMarginMemFile;
  }
  return margin_values;
}

bool MemoryPressureMonitorNotifying::SupportsKernelNotifications() {
  // Unfortunately at the moment the only way to determine if the chromeos
  // kernel supports polling on the available file is to observe two values
  // in the margin file, if the critical and moderate levels are specified
  // there then we know the kernel must support polling on available.
  return MemoryPressureMonitorNotifying::GetMarginFileParts().size() >= 2;
}

MemoryPressureListener::MemoryPressureLevel
MemoryPressureMonitorNotifying::GetCurrentPressureLevel() {
  return current_memory_pressure_level_;
}

// CheckMemoryPressure will get the current memory pressure level by reading
// the available file.
void MemoryPressureMonitorNotifying::CheckMemoryPressure() {
  auto previous_memory_pressure = current_memory_pressure_level_;
  int64_t mem_avail = ReadAvailableMemoryMB(available_mem_file_.get());
  current_memory_pressure_level_ = GetMemoryPressureLevelFromAvailable(
      mem_avail, moderate_pressure_threshold_mb_,
      critical_pressure_threshold_mb_);
  if (current_memory_pressure_level_ ==
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE) {
    last_moderate_notification_ = base::TimeTicks();
    return;
  }

  // In the case of MODERATE memory pressure we may be in this state for quite
  // some time so we limit the rate at which we dispatch notifications.
  if (current_memory_pressure_level_ ==
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE) {
    if (previous_memory_pressure == current_memory_pressure_level_) {
      if (base::TimeTicks::Now() - last_moderate_notification_ <
          kModerateMemoryPressureCooldownTime) {
        return;
      } else if (previous_memory_pressure ==
                 MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL) {
        // Reset the moderate notification time if we just crossed back.
        last_moderate_notification_ = base::TimeTicks::Now();
        return;
      }
    }

    last_moderate_notification_ = base::TimeTicks::Now();
  }

  VLOG(1) << "MemoryPressureMonitorNotifying::CheckMemoryPressure dispatching "
             "at level: "
          << current_memory_pressure_level_;
  dispatch_callback_.Run(current_memory_pressure_level_);
}

void MemoryPressureMonitorNotifying::HandleKernelNotification(bool result) {
  // If WaitForKernelNotification returned false then the FD has been closed and
  // we just exit without waiting again.
  if (!result) {
    return;
  }

  CheckMemoryPressure();

  // Now we need to schedule back our blocking task to wait for more
  // kernel notifications.
  ScheduleWaitForKernelNotification();
}

void MemoryPressureMonitorNotifying::CheckMemoryPressureAndRecordStatistics() {
  // Note: If we support notifications of memory pressure changes in both
  // directions we will not have to update the cached value as it will always
  // be correct.
  CheckMemoryPressure();

  // We only report Memory.PressureLevel every 5seconds while
  // we report ChromeOS.MemoryPressureLevel every 1s.
  if (base::TimeTicks::Now() - last_pressure_level_report_ >
      base::MemoryPressureMonitor::kUMAMemoryPressureLevelPeriod) {
    // Record to UMA "Memory.PressureLevel" a tick is 5seconds.
    RecordMemoryPressure(current_memory_pressure_level_, 1);
    last_pressure_level_report_ = base::TimeTicks::Now();
  }

  // Record UMA histogram statistics for the current memory pressure level, it
  // would seem that only Memory.PressureLevel would be necessary.
  constexpr int kNumberPressureLevels = 3;
  UMA_HISTOGRAM_ENUMERATION("ChromeOS.MemoryPressureLevel",
                            current_memory_pressure_level_,
                            kNumberPressureLevels);
}

void MemoryPressureMonitorNotifying::ScheduleEarlyCheck() {
  ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&MemoryPressureMonitorNotifying::CheckMemoryPressure,
                     weak_ptr_factory_.GetWeakPtr()));
}

void MemoryPressureMonitorNotifying::ScheduleWaitForKernelNotification() {
  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, kernel_waiting_callback_,
      base::BindRepeating(
          &MemoryPressureMonitorNotifying::HandleKernelNotification,
          weak_ptr_factory_.GetWeakPtr()));
}

void MemoryPressureMonitorNotifying::SetDispatchCallback(
    const DispatchCallback& callback) {
  dispatch_callback_ = callback;
}

// static
MemoryPressureMonitorNotifying* MemoryPressureMonitorNotifying::Get() {
  return g_monitor_notifying;
}

}  // namespace chromeos
}  // namespace base
