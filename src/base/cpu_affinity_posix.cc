// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/cpu_affinity_posix.h"

#include <sched.h>

#include "base/cpu.h"
#include "base/process/internal_linux.h"

namespace base {

bool SetThreadCpuAffinityMode(PlatformThreadId thread_id,
                              CpuAffinityMode affinity) {
  static const cpu_set_t kAllCores = []() {
    cpu_set_t set;
    memset(&set, 0xff, sizeof(set));
    return set;
  }();
  static const cpu_set_t kLittleCores = []() {
    std::vector<CPU::CoreType> core_types = CPU::GuessCoreTypes();
    if (core_types.empty())
      return kAllCores;

    cpu_set_t set;
    CPU_ZERO(&set);
    for (size_t core_index = 0; core_index < core_types.size(); core_index++) {
      switch (core_types[core_index]) {
        case CPU::CoreType::kUnknown:
        case CPU::CoreType::kOther:
        case CPU::CoreType::kSymmetric:
          // In the presence of an unknown core type or symmetric architecture,
          // fall back to allowing all cores.
          return kAllCores;
        case CPU::CoreType::kBigLittle_Little:
        case CPU::CoreType::kBigLittleBigger_Little:
          CPU_SET(core_index, &set);
          break;
        case CPU::CoreType::kBigLittle_Big:
        case CPU::CoreType::kBigLittleBigger_Big:
        case CPU::CoreType::kBigLittleBigger_Bigger:
          break;
      }
    }
    return set;
  }();

  int result = 0;
  switch (affinity) {
    case CpuAffinityMode::kDefault:
      result = sched_setaffinity(thread_id, sizeof(kAllCores), &kAllCores);
      break;
    case CpuAffinityMode::kLittleCoresOnly:
      result =
          sched_setaffinity(thread_id, sizeof(kLittleCores), &kLittleCores);
      break;
  }

  return result == 0;
}

bool SetProcessCpuAffinityMode(ProcessHandle process_handle,
                               CpuAffinityMode affinity) {
  bool any_threads = false;
  bool result = true;

  internal::ForEachProcessTask(
      process_handle, [&any_threads, &result, affinity](
                          PlatformThreadId tid, const FilePath& /*task_path*/) {
        any_threads = true;
        result &= SetThreadCpuAffinityMode(tid, affinity);
      });

  return any_threads && result;
}

}  // namespace base
