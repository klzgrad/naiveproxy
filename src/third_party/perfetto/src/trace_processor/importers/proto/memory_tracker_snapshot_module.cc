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

#include "src/trace_processor/importers/proto/memory_tracker_snapshot_module.h"
#include "src/trace_processor/importers/common/parser_types.h"

namespace perfetto {
namespace trace_processor {

using perfetto::protos::pbzero::TracePacket;

MemoryTrackerSnapshotModule::MemoryTrackerSnapshotModule(
    TraceProcessorContext* context)
    : parser_(context) {
  RegisterForField(TracePacket::kMemoryTrackerSnapshotFieldNumber, context);
}

MemoryTrackerSnapshotModule::~MemoryTrackerSnapshotModule() = default;

void MemoryTrackerSnapshotModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kMemoryTrackerSnapshotFieldNumber:
      parser_.ParseMemoryTrackerSnapshot(ts, decoder.memory_tracker_snapshot());
      return;
  }
}

void MemoryTrackerSnapshotModule::NotifyEndOfFile() {
  parser_.NotifyEndOfFile();
}

}  // namespace trace_processor
}  // namespace perfetto
