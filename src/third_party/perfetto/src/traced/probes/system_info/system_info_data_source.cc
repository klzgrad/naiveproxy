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

#include "src/traced/probes/system_info/system_info_data_source.h"

#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/traced/probes/system_info/cpu_info_features_allowlist.h"

#include "protos/perfetto/trace/system_info/cpu_info.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

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

}  // namespace

// static
const ProbesDataSource::Descriptor SystemInfoDataSource::descriptor = {
    /* name */ "linux.system_info",
    /* flags */ Descriptor::kFlagsNone,
    /* fill_descriptor_func */ nullptr,
};

SystemInfoDataSource::SystemInfoDataSource(
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer,
    std::unique_ptr<CpuFreqInfo> cpu_freq_info)
    : ProbesDataSource(session_id, &descriptor),
      writer_(std::move(writer)),
      cpu_freq_info_(std::move(cpu_freq_info)) {}

void SystemInfoDataSource::Start() {
  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  auto* cpu_info = packet->set_cpu_info();

  // Parse /proc/cpuinfo which contains groups of "key\t: value" lines separated
  // by an empty line. Each group represents a CPU. See the full example in the
  // unittest.
  std::string proc_cpu_info = ReadFile("/proc/cpuinfo");
  std::string::iterator line_start = proc_cpu_info.begin();
  std::string::iterator line_end = proc_cpu_info.end();
  std::string default_processor = "unknown";
  std::string cpu_index = "";

  std::optional<uint32_t> implementer;
  std::optional<uint32_t> architecture;
  std::optional<uint32_t> variant;
  std::optional<uint32_t> part;
  std::optional<uint32_t> revision;
  uint64_t features = 0;

  uint32_t next_cpu_index = 0;
  while (line_start != proc_cpu_info.end()) {
    line_end = find(line_start, proc_cpu_info.end(), '\n');
    if (line_end == proc_cpu_info.end())
      break;
    std::string line = std::string(line_start, line_end);
    line_start = line_end + 1;
    if (line.empty() && !cpu_index.empty()) {
      PERFETTO_DCHECK(cpu_index == std::to_string(next_cpu_index));

      auto* cpu = cpu_info->add_cpus();
      cpu->set_processor(default_processor);

      std::optional<uint32_t> cpu_capacity = base::StringToUInt32(
          base::StripSuffix(ReadFile("/sys/devices/system/cpu/cpu" + cpu_index +
                                     "/cpu_capacity"),
                            "\n"));

      if (cpu_capacity.has_value()) {
        cpu->set_capacity(cpu_capacity.value());
      }

      auto freqs_range = cpu_freq_info_->GetFreqs(next_cpu_index);
      for (auto it = freqs_range.first; it != freqs_range.second; it++) {
        cpu->add_frequencies(*it);
      }
      cpu_index = "";

      // Set Arm CPU identifier if available
      if (implementer && architecture && part && variant && revision) {
        auto* identifier = cpu->set_arm_identifier();
        identifier->set_implementer(implementer.value());
        identifier->set_architecture(architecture.value());
        identifier->set_variant(variant.value());
        identifier->set_part(part.value());
        identifier->set_revision(revision.value());
      } else if (implementer || architecture || part || variant || revision) {
        PERFETTO_ILOG("Failed to parse Arm specific fields from /proc/cpuinfo");
      }

      if (features != 0) {
        cpu->set_features(features);
      }

      implementer = std::nullopt;
      architecture = std::nullopt;
      variant = std::nullopt;
      part = std::nullopt;
      revision = std::nullopt;
      features = 0;

      next_cpu_index++;
      continue;
    }
    auto splits = base::SplitString(line, ":");
    if (splits.size() != 2)
      continue;
    std::string key =
        base::StripSuffix(base::StripChars(splits[0], "\t", ' '), " ");
    std::string value = base::StripPrefix(splits[1], " ");
    if (key == kDefaultProcessor) {
      default_processor = value;
    } else if (key == kProcessor) {
      cpu_index = value;
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
            features |= 1 << i;
          }
        }
      }
    }
  }

  packet->Finalize();
  writer_->Flush();
}

void SystemInfoDataSource::Flush(FlushRequestID,
                                 std::function<void()> callback) {
  writer_->Flush(callback);
}

std::string SystemInfoDataSource::ReadFile(std::string path) {
  std::string contents;
  if (!base::ReadFile(path, &contents))
    return "";
  return contents;
}

}  // namespace perfetto
