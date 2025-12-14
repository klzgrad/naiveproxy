/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/system_probes_module.h"

#include <cstdint>

#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/system_probes_parser.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;

SystemProbesModule::SystemProbesModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context), parser_(context) {
  RegisterForField(TracePacket::kProcessTreeFieldNumber);
  RegisterForField(TracePacket::kProcessStatsFieldNumber);
  RegisterForField(TracePacket::kSysStatsFieldNumber);
  RegisterForField(TracePacket::kSystemInfoFieldNumber);
  RegisterForField(TracePacket::kCpuInfoFieldNumber);
}

ModuleResult SystemProbesModule::TokenizePacket(
    const protos::pbzero::TracePacket::Decoder& decoder,
    TraceBlobView*,
    int64_t,
    RefPtr<PacketSequenceStateGeneration>,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kSystemInfoFieldNumber:
      parser_.ParseSystemInfo(decoder.system_info());
      return ModuleResult::Handled();
    case TracePacket::kCpuInfoFieldNumber:
      parser_.ParseCpuInfo(decoder.cpu_info());
      return ModuleResult::Handled();
  }
  return ModuleResult::Ignored();
}

void SystemProbesModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kProcessTreeFieldNumber:
      parser_.ParseProcessTree(ts, decoder.process_tree());
      return;
    case TracePacket::kProcessStatsFieldNumber:
      parser_.ParseProcessStats(ts, decoder.process_stats());
      return;
    case TracePacket::kSysStatsFieldNumber:
      parser_.ParseSysStats(ts, decoder.sys_stats());
      return;
  }
}

}  // namespace perfetto::trace_processor
