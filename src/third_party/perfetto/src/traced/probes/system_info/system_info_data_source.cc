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
#include "perfetto/ext/base/cpu_info.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"

#include "protos/perfetto/trace/system_info/cpu_info.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

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

  for (const auto& parsed_cpu : ReadCpuInfo()) {
    auto* cpu = cpu_info->add_cpus();
    cpu->set_processor(parsed_cpu.processor);

    std::optional<uint32_t> cpu_capacity =
        base::StringToUInt32(base::StripSuffix(
            ReadFile("/sys/devices/system/cpu/cpu" +
                     std::to_string(parsed_cpu.cpu_index) + "/cpu_capacity"),
            "\n"));

    if (cpu_capacity.has_value()) {
      cpu->set_capacity(cpu_capacity.value());
    }

    auto freqs_range = cpu_freq_info_->GetFreqs(parsed_cpu.cpu_index);
    for (auto it = freqs_range.first; it != freqs_range.second; it++) {
      cpu->add_frequencies(*it);
    }

    if (parsed_cpu.implementer && parsed_cpu.architecture && parsed_cpu.part &&
        parsed_cpu.variant && parsed_cpu.revision) {
      auto* identifier = cpu->set_arm_identifier();
      identifier->set_implementer(parsed_cpu.implementer.value());
      identifier->set_architecture(parsed_cpu.architecture.value());
      identifier->set_variant(parsed_cpu.variant.value());
      identifier->set_part(parsed_cpu.part.value());
      identifier->set_revision(parsed_cpu.revision.value());
    } else if (parsed_cpu.implementer || parsed_cpu.architecture ||
               parsed_cpu.part || parsed_cpu.variant || parsed_cpu.revision) {
      PERFETTO_DLOG("Arm specific fields not found for cpu %" PRIu32,
                    parsed_cpu.cpu_index);
    }

    if (parsed_cpu.features != 0) {
      cpu->set_features(parsed_cpu.features);
    }
  }

  packet->Finalize();
  writer_->Flush();
}

void SystemInfoDataSource::Flush(FlushRequestID,
                                 std::function<void()> callback) {
  writer_->Flush(callback);
}

std::vector<base::CpuInfo> SystemInfoDataSource::ReadCpuInfo() {
  return base::ReadCpuInfo();
}

std::string SystemInfoDataSource::ReadFile(std::string path) {
  std::string contents;
  if (!base::ReadFile(path, &contents))
    return "";
  return contents;
}

}  // namespace perfetto
