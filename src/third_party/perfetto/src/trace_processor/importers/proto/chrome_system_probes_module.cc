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
#include "perfetto/base/build_config.h"
#include "src/trace_processor/importers/proto/chrome_system_probes_parser.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace trace_processor {

using perfetto::protos::pbzero::TracePacket;

ChromeSystemProbesModule::ChromeSystemProbesModule(
    TraceProcessorContext* context)
    : parser_(context) {
  RegisterForField(TracePacket::kProcessStatsFieldNumber, context);
}

void ChromeSystemProbesModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kProcessStatsFieldNumber:
      parser_.ParseProcessStats(ts, decoder.process_stats());
      return;
  }
}

}  // namespace trace_processor
}  // namespace perfetto
