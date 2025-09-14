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

namespace perfetto {
namespace trace_processor {

using perfetto::protos::pbzero::TracePacket;

GenericKernelModule::GenericKernelModule(TraceProcessorContext* context)
    : parser_(context) {
  RegisterForField(TracePacket::kGenericKernelTaskStateEventFieldNumber,
                   context);
  RegisterForField(TracePacket::kGenericKernelTaskRenameEventFieldNumber,
                   context);
  RegisterForField(TracePacket::kGenericKernelCpuFreqEventFieldNumber, context);
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
    case TracePacket::kGenericKernelCpuFreqEventFieldNumber:
      parser_.ParseGenericCpuFrequencyEvent(
          ts, decoder.generic_kernel_cpu_freq_event());
      return;
  }
}

}  // namespace trace_processor
}  // namespace perfetto
