// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

#include <windows.h>  // Must be in front of other Windows header files.

#include <psapi.h>
#include <stddef.h>
#include <stdint.h>
#include <winternl.h>

#include <algorithm>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/process/memory.h"
#include "base/process/process_metrics_iocounters.h"
#include "base/sys_info.h"

namespace base {
namespace {

// System pagesize. This value remains constant on x86/64 architectures.
const int PAGESIZE_KB = 4;

typedef NTSTATUS(WINAPI* NTQUERYSYSTEMINFORMATION)(
    SYSTEM_INFORMATION_CLASS SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength);

}  // namespace

ProcessMetrics::~ProcessMetrics() { }

size_t GetMaxFds() {
  // Windows is only limited by the amount of physical memory.
  return std::numeric_limits<size_t>::max();
}

// static
std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateProcessMetrics(
    ProcessHandle process) {
  return WrapUnique(new ProcessMetrics(process));
}

namespace {

class WorkingSetInformationBuffer {
 public:
  WorkingSetInformationBuffer() {}
  ~WorkingSetInformationBuffer() { Clear(); }

  bool Reserve(size_t size) {
    Clear();
    // Use UncheckedMalloc here because this can be called from the code
    // that handles low memory condition.
    return UncheckedMalloc(size, reinterpret_cast<void**>(&buffer_));
  }

  const PSAPI_WORKING_SET_INFORMATION* operator ->() const { return buffer_; }

  size_t GetPageEntryCount() const { return number_of_entries; }

  // This function is used to get page entries for a process.
  bool QueryPageEntries(const ProcessHandle& process) {
    int retries = 5;
    number_of_entries = 4096;  // Just a guess.

    for (;;) {
      size_t buffer_size =
          sizeof(PSAPI_WORKING_SET_INFORMATION) +
          (number_of_entries * sizeof(PSAPI_WORKING_SET_BLOCK));

      if (!Reserve(buffer_size))
        return false;

      // On success, |buffer_| is populated with info about the working set of
      // |process|. On ERROR_BAD_LENGTH failure, increase the size of the
      // buffer and try again.
      if (QueryWorkingSet(process, buffer_, buffer_size))
        break;  // Success

      if (GetLastError() != ERROR_BAD_LENGTH)
        return false;

      number_of_entries = buffer_->NumberOfEntries;

      // Maybe some entries are being added right now. Increase the buffer to
      // take that into account. Increasing by 10% should generally be enough,
      // especially considering the potentially low memory condition during the
      // call (when called from OomMemoryDetails) and the potentially high
      // number of entries (300K was observed in crash dumps).
      number_of_entries *= 1.1;

      if (--retries == 0) {
        // If we're looping, eventually fail.
        return false;
      }
    }

    // TODO(chengx): Remove the comment and the logic below. It is no longer
    // needed since we don't have Win2000 support.
    // On windows 2000 the function returns 1 even when the buffer is too small.
    // The number of entries that we are going to parse is the minimum between
    // the size we allocated and the real number of entries.
    number_of_entries = std::min(number_of_entries,
                                 static_cast<size_t>(buffer_->NumberOfEntries));

    return true;
  }

 private:
  void Clear() {
    free(buffer_);
    buffer_ = nullptr;
  }

  PSAPI_WORKING_SET_INFORMATION* buffer_ = nullptr;

  // Number of page entries.
  size_t number_of_entries = 0;

  DISALLOW_COPY_AND_ASSIGN(WorkingSetInformationBuffer);
};

}  // namespace

TimeDelta ProcessMetrics::GetCumulativeCPUUsage() {
  FILETIME creation_time;
  FILETIME exit_time;
  FILETIME kernel_time;
  FILETIME user_time;

  if (!GetProcessTimes(process_.Get(), &creation_time, &exit_time, &kernel_time,
                       &user_time)) {
    // We don't assert here because in some cases (such as in the Task Manager)
    // we may call this function on a process that has just exited but we have
    // not yet received the notification.
    return TimeDelta();
  }

  return TimeDelta::FromFileTime(kernel_time) +
         TimeDelta::FromFileTime(user_time);
}

bool ProcessMetrics::GetIOCounters(IoCounters* io_counters) const {
  return GetProcessIoCounters(process_.Get(), io_counters) != FALSE;
}

ProcessMetrics::ProcessMetrics(ProcessHandle process) {
  if (process) {
    HANDLE duplicate_handle = INVALID_HANDLE_VALUE;
    BOOL result = ::DuplicateHandle(::GetCurrentProcess(), process,
                                    ::GetCurrentProcess(), &duplicate_handle,
                                    PROCESS_QUERY_INFORMATION, FALSE, 0);
    DPCHECK(result);
    process_.Set(duplicate_handle);
  }
}

size_t GetSystemCommitCharge() {
  // Get the System Page Size.
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);

  PERFORMANCE_INFORMATION info;
  if (!GetPerformanceInfo(&info, sizeof(info))) {
    DLOG(ERROR) << "Failed to fetch internal performance info.";
    return 0;
  }
  return (info.CommitTotal * system_info.dwPageSize) / 1024;
}

size_t GetPageSize() {
  return PAGESIZE_KB * 1024;
}

// This function uses the following mapping between MEMORYSTATUSEX and
// SystemMemoryInfoKB:
//   ullTotalPhys ==> total
//   ullAvailPhys ==> avail_phys
//   ullTotalPageFile ==> swap_total
//   ullAvailPageFile ==> swap_free
bool GetSystemMemoryInfo(SystemMemoryInfoKB* meminfo) {
  MEMORYSTATUSEX mem_status;
  mem_status.dwLength = sizeof(mem_status);
  if (!::GlobalMemoryStatusEx(&mem_status))
    return false;

  meminfo->total = mem_status.ullTotalPhys / 1024;
  meminfo->avail_phys = mem_status.ullAvailPhys / 1024;
  meminfo->swap_total = mem_status.ullTotalPageFile / 1024;
  meminfo->swap_free = mem_status.ullAvailPageFile / 1024;

  return true;
}

size_t ProcessMetrics::GetMallocUsage() {
  // Unsupported as getting malloc usage on Windows requires iterating through
  // the heap which is slow and crashes.
  return 0;
}

}  // namespace base
