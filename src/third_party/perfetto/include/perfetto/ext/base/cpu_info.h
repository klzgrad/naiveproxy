/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_EXT_BASE_CPU_INFO_H_
#define INCLUDE_PERFETTO_EXT_BASE_CPU_INFO_H_

#include <stdint.h>
#include <optional>
#include <string>
#include <vector>

namespace perfetto {
namespace base {

struct CpuInfo {
  std::string processor;
  uint32_t cpu_index = 0;
  std::optional<uint32_t> implementer;
  std::optional<uint32_t> architecture;
  std::optional<uint32_t> variant;
  std::optional<uint32_t> part;
  std::optional<uint32_t> revision;
  uint64_t features = 0;
  char arm_cpuid[32] = {};
};

// Parses the contents of the input string into per-CPU entries.
std::vector<CpuInfo> ParseCpuInfo(std::string proc_cpu_info);

// Reads /proc/cpuinfo and parses it into per-CPU entries.
std::vector<CpuInfo> ReadCpuInfo();

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_CPU_INFO_H_
