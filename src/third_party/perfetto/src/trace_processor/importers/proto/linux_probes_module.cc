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

#include "src/trace_processor/importers/proto/linux_probes_module.h"

#include <cstdint>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;

LinuxProbesModule::LinuxProbesModule(ProtoImporterModuleContext* module_context,
                                     TraceProcessorContext* context)
    : ProtoImporterModule(module_context), parser_(context) {
  RegisterForField(TracePacket::kJournaldEventFieldNumber);
}

void LinuxProbesModule::ParseField(const ParseFieldArgs& args) {
  switch (args.field.id()) {
    case TracePacket::kJournaldEventFieldNumber:
      parser_.ParseSystemdJournaldEvent(
          args.ts, args.field.Cast<TracePacket::kJournaldEvent>());
      return;
    default:
      PERFETTO_FATAL("Unexpected field_id in LinuxProbesModule");
  }
}

}  // namespace perfetto::trace_processor
