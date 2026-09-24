/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/profiling/common/profiler_guardrails.h"

#include <unistd.h>
#include <algorithm>
#include <optional>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/watchdog_posix.h"

namespace perfetto {
namespace profiling {

std::optional<uint64_t> GetCputimeSecForCurrentProcess() {
  return GetCputimeSecForCurrentProcess(
      base::OpenFile("/proc/self/stat", O_RDONLY));
}

std::optional<uint64_t> GetCputimeSecForCurrentProcess(
    base::ScopedFile stat_fd) {
  if (!stat_fd)
    return std::nullopt;
  base::ProcStat stat;
  if (!ReadProcStat(stat_fd.get(), &stat)) {
    PERFETTO_ELOG("Failed to read stat file to enforce guardrails.");
    return std::nullopt;
  }
  return (stat.utime + stat.stime) /
         static_cast<unsigned long>(sysconf(_SC_CLK_TCK));
}

ProfilerMemoryGuardrails::ProfilerMemoryGuardrails()
    : ProfilerMemoryGuardrails(base::OpenFile("/proc/self/status", O_RDONLY)) {}

ProfilerMemoryGuardrails::ProfilerMemoryGuardrails(base::ScopedFile status_fd) {
  std::string status;
  if (base::ReadFileDescriptor(*status_fd, &status))
    anon_and_swap_ = GetRssAnonAndSwap(status);

  if (!anon_and_swap_) {
    PERFETTO_ELOG("Failed to read memory usage.");
    return;
  }
}

bool ProfilerMemoryGuardrails::IsOverMemoryThreshold(
    const GuardrailConfig& ds) {
  uint32_t ds_max_mem = ds.memory_guardrail_kb;
  if (!ds_max_mem || !anon_and_swap_)
    return false;

  if (ds_max_mem > 0 && *anon_and_swap_ > ds_max_mem) {
    PERFETTO_ELOG("Exceeded data-source memory guardrail (%" PRIu32
                  " > %" PRIu32 "). Shutting down.",
                  *anon_and_swap_, ds_max_mem);
    return true;
  }
  return false;
}

ProfilerCpuGuardrails::ProfilerCpuGuardrails() {
  opt_cputime_sec_ = GetCputimeSecForCurrentProcess();
  if (!opt_cputime_sec_) {
    PERFETTO_ELOG("Failed to get CPU time.");
  }
}

// For testing.
ProfilerCpuGuardrails::ProfilerCpuGuardrails(base::ScopedFile stat_fd) {
  opt_cputime_sec_ = GetCputimeSecForCurrentProcess(std::move(stat_fd));
  if (!opt_cputime_sec_) {
    PERFETTO_ELOG("Failed to get CPU time.");
  }
}

bool ProfilerCpuGuardrails::IsOverCpuThreshold(const GuardrailConfig& ds) {
  uint64_t ds_max_cpu = ds.cpu_guardrail_sec;
  if (!ds_max_cpu || !opt_cputime_sec_)
    return false;
  uint64_t cputime_sec = *opt_cputime_sec_;

  auto start_cputime_sec = ds.cpu_start_secs;
  // We reject data-sources with CPU guardrails if we cannot read the
  // initial value, which means we get a non-nullopt value here.
  PERFETTO_CHECK(start_cputime_sec);
  uint64_t cpu_diff = cputime_sec - *start_cputime_sec;
  if (cputime_sec > *start_cputime_sec && cpu_diff > ds_max_cpu) {
    PERFETTO_ELOG("Exceeded data-source CPU guardrail (%" PRIu64 " > %" PRIu64
                  "). Shutting down.",
                  cpu_diff, ds_max_cpu);
    return true;
  }
  return false;
}

}  // namespace profiling
}  // namespace perfetto
