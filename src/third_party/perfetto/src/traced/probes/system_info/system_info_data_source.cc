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
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/tracing/core/data_source_config.h"

#include "protos/perfetto/config/system_info/system_info_config.gen.h"
#include "protos/perfetto/trace/system_info/cpu_info.pbzero.h"
#include "protos/perfetto/trace/system_info/interrupt_info.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {

struct InterruptMapping {
  uint32_t irq_id;
  std::string name;
};

std::vector<InterruptMapping> ReadInterruptMappings(std::string content) {
  std::vector<InterruptMapping> mappings;

  base::StringSplitter lines(std::move(content), '\n');
  if (!lines.Next())
    return mappings;

  // Count CPUs from the header line (tokens starting with "CPU").
  size_t num_cpus = 0;
  {
    base::StringSplitter header_tok(&lines, ' ');
    while (header_tok.Next()) {
      if (base::StartsWith(header_tok.cur_token(), "CPU"))
        num_cpus++;
    }
  }

  if (num_cpus == 0)
    return mappings;

  while (lines.Next()) {
    std::vector<std::string> tokens;
    {
      base::StringSplitter tok(&lines, ' ');
      while (tok.Next())
        tokens.emplace_back(tok.cur_token(), tok.cur_token_size());
    }

    if (tokens.size() <= num_cpus)
      continue;

    // First token must be numeric "IRQ_NUM:". Non-numeric pseudo-IRQ rollup
    // lines are intentionally skipped (e.g. "NMI: Non-maskable interrupts",
    // "LOC: Local timer interrupts").
    const std::string& id_token = tokens[0];
    if (id_token.empty() || id_token.back() != ':')
      continue;
    auto irq_id_opt =
        base::CStringToUInt32(id_token.substr(0, id_token.size() - 1).c_str());
    if (!irq_id_opt)
      continue;
    uint32_t irq_id = *irq_id_opt;

    // Find the trigger token to anchor the name. ARM GIC uses standalone
    // "Level"/"Edge"; x86 IO-APIC embeds the trigger in the hw-irq field
    // (e.g. "16-fasteoi", "2-edge"). Searching backwards is efficient since
    // the trigger appears within a few tokens of the end. Handles multi-word
    // chip names (e.g. "cs40l26 IRQ1 Controller"). Falls back to the last
    // token when no trigger is recognised.
    auto is_trigger = [](const std::string& t) {
      return t == "Level" || t == "Edge" || base::EndsWith(t, "-edge") ||
             base::EndsWith(t, "-fasteoi") || base::EndsWith(t, "-level") ||
             base::EndsWith(t, "-fasteoi-level");
    };

    ssize_t trigger_idx = -1;
    for (size_t i = tokens.size() - 1; i > num_cpus; --i) {
      if (is_trigger(tokens[i])) {
        trigger_idx = static_cast<ssize_t>(i);
        break;
      }
    }

    size_t name_start = (trigger_idx != -1)
                            ? static_cast<size_t>(trigger_idx + 1)
                            : tokens.size() - 1;

    if (name_start >= tokens.size() || name_start < num_cpus + 2)
      continue;

    InterruptMapping mapping;
    mapping.irq_id = irq_id;
    for (size_t j = name_start; j < tokens.size(); ++j) {
      if (!mapping.name.empty())
        mapping.name += " ";
      mapping.name += tokens[j];
    }

    if (!mapping.name.empty())
      mappings.push_back(std::move(mapping));
  }

  return mappings;
}

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
    std::unique_ptr<CpuFreqInfo> cpu_freq_info,
    const DataSourceConfig& config)
    : ProbesDataSource(session_id, &descriptor),
      writer_(std::move(writer)),
      cpu_freq_info_(std::move(cpu_freq_info)),
      include_irq_mapping_(config.system_info_config().irq_names()) {}

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

  if (include_irq_mapping_) {
    auto mappings = ReadInterruptMappings(ReadFile("/proc/interrupts"));
    if (!mappings.empty()) {
      auto irq_packet = writer_->NewTracePacket();
      irq_packet->set_timestamp(
          static_cast<uint64_t>(base::GetBootTimeNs().count()));
      auto* interrupt_info = irq_packet->set_interrupt_info();
      for (const auto& mapping : mappings) {
        auto* irq_proto = interrupt_info->add_irq_mapping();
        irq_proto->set_irq_id(mapping.irq_id);
        irq_proto->set_name(mapping.name);
      }
    }
  }
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
