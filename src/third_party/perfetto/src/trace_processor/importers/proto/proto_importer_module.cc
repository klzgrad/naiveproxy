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

#include "src/trace_processor/importers/proto/proto_importer_module.h"

#include "perfetto/trace_processor/ref_counted.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

ProtoImporterModule::ProtoImporterModule() {}

ProtoImporterModule::~ProtoImporterModule() {}

ModuleResult ProtoImporterModule::TokenizePacket(
    const protos::pbzero::TracePacket_Decoder&,
    TraceBlobView* /*packet*/,
    int64_t /*packet_timestamp*/,
    RefPtr<PacketSequenceStateGeneration> /*sequence_state*/,
    uint32_t /*field_id*/) {
  return ModuleResult::Ignored();
}

void ProtoImporterModule::ParseTracePacketData(
    const protos::pbzero::TracePacket_Decoder&,
    int64_t /*ts*/,
    const TracePacketData&,
    uint32_t /*field_id*/) {}

void ProtoImporterModule::ParseTraceConfig(
    const protos::pbzero::TraceConfig_Decoder&) {}

void ProtoImporterModule::RegisterForField(uint32_t field_id,
                                           TraceProcessorContext* context) {
  if (context->modules_by_field.size() <= field_id) {
    context->modules_by_field.resize(field_id + 1);
  }
  context->modules_by_field[field_id].push_back(this);
}

void ProtoImporterModule::RegisterForAllFields(TraceProcessorContext* context) {
  context->modules_for_all_fields.push_back(this);
}

}  // namespace trace_processor
}  // namespace perfetto
