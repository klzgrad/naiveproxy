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

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/ext/base/cpu_info.h"
#include "perfetto/ext/base/cpu_info_features_allowlist.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"

namespace perfetto {
namespace base {
namespace {

// Key for default processor string in /proc/cpuinfo as seen on arm. Note the
// uppercase P.
const char kDefaultProcessor[] = "Processor";

// Key for processor entry in /proc/cpuinfo. Used to determine whether a group
// of lines describes a CPU.
const char kProcessor[] = "processor";

// Key for CPU implementer in /proc/cpuinfo. Arm only.
const char kImplementer[] = "CPU implementer";

// Key for CPU architecture in /proc/cpuinfo. Arm only.
const char kArchitecture[] = "CPU architecture";

// Key for CPU variant in /proc/cpuinfo. Arm only.
const char kVariant[] = "CPU variant";

// Key for CPU part in /proc/cpuinfo. Arm only.
const char kPart[] = "CPU part";

// Key for CPU revision in /proc/cpuinfo. Arm only.
const char kRevision[] = "CPU revision";

// Key for feature flags in /proc/cpuinfo. Arm calls them Features,
// Intel calls them Flags.
const char kFeatures[] = "Features";
const char kFlags[] = "Flags";

std::string ReadFile(const std::string& path) {
  std::string contents;
  if (!base::ReadFile(path, &contents))
    return "";
  return contents;
}

}  // namespace

std::vector<CpuInfo> ParseCpuInfo(std::string proc_cpu_info) {
  std::vector<CpuInfo> cpus;
  std::string processor = "unknown";

  std::optional<uint32_t> cpu_index;
  std::optional<uint32_t> implementer;
  std::optional<uint32_t> architecture;
  std::optional<uint32_t> variant;
  std::optional<uint32_t> part;
  std::optional<uint32_t> revision;
  uint64_t features = 0;
  uint32_t next_cpu_index = 0;

  auto flush_cpu = [&] {
    if (cpu_index.has_value()) {
      CpuInfo cpu{};
      cpu.processor = processor;
      cpu.cpu_index = *cpu_index;
      cpu.implementer = implementer;
      cpu.architecture = architecture;
      cpu.variant = variant;
      cpu.part = part;
      cpu.revision = revision;
      cpu.features = features;
#if PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)
      if (cpu.implementer && cpu.part) {
        std::string cpuid =
            base::Uint64ToHexStringNoPrefix(cpu.implementer.value()) +
            base::Uint64ToHexStringNoPrefix(cpu.part.value());
        if (cpu.variant) {
          cpuid += base::Uint64ToHexStringNoPrefix(cpu.variant.value());
          if (cpu.revision) {
            cpuid += base::Uint64ToHexStringNoPrefix(cpu.revision.value());
          }
        }
        base::StringCopy(cpu.arm_cpuid, cpuid.c_str(), sizeof(cpu.arm_cpuid));
      }
#endif  // PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_ARM64)
      cpus.emplace_back(std::move(cpu));
      next_cpu_index++;
    }
    cpu_index = std::nullopt;
    implementer = std::nullopt;
    architecture = std::nullopt;
    variant = std::nullopt;
    part = std::nullopt;
    revision = std::nullopt;
    features = 0;
  };

  for (base::StringSplitter lines(
           std::move(proc_cpu_info), '\n',
           base::StringSplitter::EmptyTokenMode::ALLOW_EMPTY_TOKENS);
       lines.Next();) {
    std::string line(lines.cur_token(), lines.cur_token_size());
    if (line.empty() && cpu_index.has_value()) {
      flush_cpu();
      continue;
    }

    auto splits = base::SplitString(line, ":");
    if (splits.size() != 2)
      continue;
    std::string key =
        base::StripSuffix(base::StripChars(splits[0], "\t", ' '), " ");
    std::string value = base::StripPrefix(splits[1], " ");

    if (key == kDefaultProcessor) {
      processor = value;
    } else if (key == kProcessor) {
      cpu_index = base::StringToUInt32(value);
    } else if (key == kImplementer) {
      implementer = base::CStringToUInt32(value.data(), 16);
    } else if (key == kArchitecture) {
      architecture = base::CStringToUInt32(value.data(), 10);
    } else if (key == kVariant) {
      variant = base::CStringToUInt32(value.data(), 16);
    } else if (key == kPart) {
      part = base::CStringToUInt32(value.data(), 16);
    } else if (key == kRevision) {
      revision = base::CStringToUInt32(value.data(), 10);
    } else if (key == kFeatures || key == kFlags) {
      for (base::StringSplitter ss(value.data(), ' '); ss.Next();) {
        for (size_t i = 0; i < base::ArraySize(kCpuInfoFeatures); ++i) {
          if (strcmp(ss.cur_token(), kCpuInfoFeatures[i]) == 0) {
            static_assert(base::ArraySize(kCpuInfoFeatures) < 64);
            features |= 1ull << i;
          }
        }
      }
    }
  }

  flush_cpu();
  return cpus;
}

std::vector<CpuInfo> ReadCpuInfo() {
  return ParseCpuInfo(ReadFile("/proc/cpuinfo"));
}

}  // namespace base
}  // namespace perfetto
