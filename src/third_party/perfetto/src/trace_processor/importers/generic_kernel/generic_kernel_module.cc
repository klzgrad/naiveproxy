/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/importers/generic_kernel/generic_kernel_module.h"

#include <cstdint>

#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/generic_kernel/generic_kernel_parser.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;

GenericKernelModule::GenericKernelModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context), parser_(context) {
  RegisterForField(TracePacket::kGenericKernelCpuFreqEventFieldNumber);
  RegisterForField(TracePacket::kGenericKernelProcessTreeFieldNumber);
  RegisterForField(TracePacket::kGenericKernelTaskStateEventFieldNumber);
  RegisterForField(TracePacket::kGenericKernelTaskRenameEventFieldNumber);
}

void GenericKernelModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kGenericKernelTaskStateEventFieldNumber:
      parser_.ParseGenericTaskStateEvent(
          ts, decoder.generic_kernel_task_state_event());
      return;
    case TracePacket::kGenericKernelTaskRenameEventFieldNumber:
      parser_.ParseGenericTaskRenameEvent(
          decoder.generic_kernel_task_rename_event());
      return;
    case TracePacket::kGenericKernelProcessTreeFieldNumber:
      parser_.ParseGenericProcessTree(decoder.generic_kernel_process_tree());
      return;
    case TracePacket::kGenericKernelCpuFreqEventFieldNumber:
      parser_.ParseGenericCpuFrequencyEvent(
          ts, decoder.generic_kernel_cpu_freq_event());
      return;
  }
}

}  // namespace perfetto::trace_processor
