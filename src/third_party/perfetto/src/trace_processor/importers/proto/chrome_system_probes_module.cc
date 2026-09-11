/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/chrome_system_probes_module.h"
#include <cstdint>

#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/chrome_system_probes_parser.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;

ChromeSystemProbesModule::ChromeSystemProbesModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context), parser_(context) {
  RegisterForField(TracePacket::kProcessStatsFieldNumber);
}

void ChromeSystemProbesModule::ParseField(const ParseFieldArgs& args) {
  switch (args.field.id()) {
    case TracePacket::kProcessStatsFieldNumber:
      parser_.ParseProcessStats(args.ts,
                                args.field.Cast<TracePacket::kProcessStats>());
      return;
  }
}

}  // namespace perfetto::trace_processor
