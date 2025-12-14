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

#include "src/trace_processor/importers/proto/deobfuscation_module.h"

#include "perfetto/ext/base/flat_hash_map.h"
#include "protos/perfetto/trace/profiling/deobfuscation.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/deobfuscation_mapping_table.h"
#include "src/trace_processor/importers/proto/deobfuscation_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

using ::perfetto::protos::pbzero::TracePacket;
using ::protozero::ConstBytes;

DeobfuscationModule::DeobfuscationModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context), context_(context) {
  RegisterForField(TracePacket::kDeobfuscationMappingFieldNumber);
}

DeobfuscationModule::~DeobfuscationModule() = default;

void DeobfuscationModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kDeobfuscationMappingFieldNumber:
      StoreDeobfuscationMapping(decoder.deobfuscation_mapping());
      return;
    default:
      break;
  }
}

void DeobfuscationModule::StoreDeobfuscationMapping(ConstBytes blob) {
  DeobfuscationTracker::Get(context_)->AddDeobfuscationMapping(blob);

  protos::pbzero::DeobfuscationMapping::Decoder mapping(blob.data, blob.size);
  BuildMappingTableIncremental(mapping);
}

void DeobfuscationModule::BuildMappingTableIncremental(
    const protos::pbzero::DeobfuscationMapping::Decoder& mapping) {
  if (mapping.package_name().size == 0)
    return;

  DeobfuscationMappingTable::PackageId package_id{
      mapping.package_name().ToStdString(), mapping.version_code()};

  for (auto class_it = mapping.obfuscated_classes(); class_it; ++class_it) {
    protos::pbzero::ObfuscatedClass::Decoder cls(*class_it);

    base::FlatHashMap<StringId, StringId> members;
    for (auto member_it = cls.obfuscated_methods(); member_it; ++member_it) {
      protos::pbzero::ObfuscatedMember::Decoder member(*member_it);
      members[context_->storage->InternString(member.obfuscated_name())] =
          context_->storage->InternString(member.deobfuscated_name());
    }

    context_->args_translation_table->MergeDeobfuscationMapping(
        package_id, context_->storage->InternString(cls.obfuscated_name()),
        context_->storage->InternString(cls.deobfuscated_name()),
        std::move(members));
  }
}

void DeobfuscationModule::NotifyEndOfFile() {}

}  // namespace perfetto::trace_processor
