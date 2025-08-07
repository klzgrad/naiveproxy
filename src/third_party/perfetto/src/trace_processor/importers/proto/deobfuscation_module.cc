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

#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/profiling/deobfuscation.pbzero.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/deobfuscation_mapping_table.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/proto/heap_graph_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/profiler_util.h"

namespace perfetto::trace_processor {

using ::perfetto::protos::pbzero::TracePacket;
using ::protozero::ConstBytes;

DeobfuscationModule::DeobfuscationModule(TraceProcessorContext* context)
    : context_(context) {
  RegisterForField(TracePacket::kDeobfuscationMappingFieldNumber, context);
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
  packets_.emplace_back(TraceBlob::CopyFrom(blob.data, blob.size));
}

void DeobfuscationModule::DeobfuscateHeapGraphClass(
    std::optional<StringId> package_name_id,
    StringId obfuscated_class_name_id,
    const protos::pbzero::ObfuscatedClass::Decoder& cls) {
  using ClassTable = tables::HeapGraphClassTable;

  auto* heap_graph_tracker = HeapGraphTracker::GetOrCreate(context_);
  const std::vector<ClassTable::RowNumber>* cls_objects =
      heap_graph_tracker->RowsForType(package_name_id,
                                      obfuscated_class_name_id);
  if (cls_objects) {
    auto* class_table = context_->storage->mutable_heap_graph_class_table();
    for (ClassTable::RowNumber class_row_num : *cls_objects) {
      auto class_ref = class_row_num.ToRowReference(class_table);
      const StringId obfuscated_type_name_id = class_ref.name();
      const base::StringView obfuscated_type_name =
          context_->storage->GetString(obfuscated_type_name_id);
      NormalizedType normalized_type = GetNormalizedType(obfuscated_type_name);
      std::string deobfuscated_type_name =
          DenormalizeTypeName(normalized_type, cls.deobfuscated_name());
      StringId deobfuscated_type_name_id = context_->storage->InternString(
          base::StringView(deobfuscated_type_name));
      class_ref.set_deobfuscated_name(deobfuscated_type_name_id);
    }
  } else {
    PERFETTO_DLOG("Class %s not found",
                  cls.obfuscated_name().ToStdString().c_str());
  }
}

void DeobfuscationModule::ParseDeobfuscationMapping(
    ConstBytes blob,
    HeapGraphTracker* heap_graph_tracker) {
  protos::pbzero::DeobfuscationMapping::Decoder deobfuscation_mapping(
      blob.data, blob.size);
  ParseDeobfuscationMappingForHeapGraph(deobfuscation_mapping,
                                        heap_graph_tracker);
  ParseDeobfuscationMappingForProfiles(deobfuscation_mapping);
}

void DeobfuscationModule::ParseDeobfuscationMappingForHeapGraph(
    const protos::pbzero::DeobfuscationMapping::Decoder& deobfuscation_mapping,
    HeapGraphTracker* heap_graph_tracker) {
  using ReferenceTable = tables::HeapGraphReferenceTable;

  std::optional<StringId> package_name_id;
  if (deobfuscation_mapping.package_name().size > 0) {
    package_name_id = context_->storage->string_pool().GetId(
        deobfuscation_mapping.package_name());
  }

  auto* reference_table =
      context_->storage->mutable_heap_graph_reference_table();
  for (auto class_it = deobfuscation_mapping.obfuscated_classes(); class_it;
       ++class_it) {
    protos::pbzero::ObfuscatedClass::Decoder cls(*class_it);
    auto obfuscated_class_name_id =
        context_->storage->string_pool().GetId(cls.obfuscated_name());
    if (!obfuscated_class_name_id) {
      PERFETTO_DLOG("Class string %s not found",
                    cls.obfuscated_name().ToStdString().c_str());
    } else {
      // TODO(b/153552977): Remove this work-around for legacy traces.
      // For traces without location information, deobfuscate all matching
      // classes.
      DeobfuscateHeapGraphClass(std::nullopt, *obfuscated_class_name_id, cls);
      if (package_name_id) {
        DeobfuscateHeapGraphClass(package_name_id, *obfuscated_class_name_id,
                                  cls);
      }
    }
    for (auto member_it = cls.obfuscated_members(); member_it; ++member_it) {
      protos::pbzero::ObfuscatedMember::Decoder member(*member_it);

      std::string merged_obfuscated = cls.obfuscated_name().ToStdString() +
                                      "." +
                                      member.obfuscated_name().ToStdString();
      std::string merged_deobfuscated =
          FullyQualifiedDeobfuscatedName(cls, member);

      auto obfuscated_field_name_id = context_->storage->string_pool().GetId(
          base::StringView(merged_obfuscated));
      if (!obfuscated_field_name_id) {
        PERFETTO_DLOG("Field string %s not found", merged_obfuscated.c_str());
        continue;
      }

      const std::vector<ReferenceTable::RowNumber>* field_references =
          heap_graph_tracker->RowsForField(*obfuscated_field_name_id);
      if (field_references) {
        auto interned_deobfuscated_name = context_->storage->InternString(
            base::StringView(merged_deobfuscated));
        for (ReferenceTable::RowNumber row_number : *field_references) {
          auto row_ref = row_number.ToRowReference(reference_table);
          row_ref.set_deobfuscated_field_name(interned_deobfuscated_name);
        }
      } else {
        PERFETTO_DLOG("Field %s not found", merged_obfuscated.c_str());
      }
    }
  }
}

void DeobfuscationModule::ParseDeobfuscationMappingForProfiles(
    const protos::pbzero::DeobfuscationMapping::Decoder&
        deobfuscation_mapping) {
  DeobfuscationMappingTable deobfuscation_mapping_table;
  if (deobfuscation_mapping.package_name().size == 0)
    return;

  auto opt_package_name_id = context_->storage->string_pool().GetId(
      deobfuscation_mapping.package_name());
  auto opt_memfd_id = context_->storage->string_pool().GetId("memfd");
  if (!opt_package_name_id && !opt_memfd_id)
    return;

  for (auto class_it = deobfuscation_mapping.obfuscated_classes(); class_it;
       ++class_it) {
    protos::pbzero::ObfuscatedClass::Decoder cls(*class_it);
    base::FlatHashMap<StringId, StringId> obfuscated_to_deobfuscated_members;
    for (auto member_it = cls.obfuscated_methods(); member_it; ++member_it) {
      protos::pbzero::ObfuscatedMember::Decoder member(*member_it);
      std::string merged_obfuscated = cls.obfuscated_name().ToStdString() +
                                      "." +
                                      member.obfuscated_name().ToStdString();
      auto merged_obfuscated_id = context_->storage->string_pool().GetId(
          base::StringView(merged_obfuscated));
      if (!merged_obfuscated_id)
        continue;
      std::string merged_deobfuscated =
          FullyQualifiedDeobfuscatedName(cls, member);

      std::vector<tables::StackProfileFrameTable::Id> frames;
      if (opt_package_name_id) {
        const std::vector<tables::StackProfileFrameTable::Id> pkg_frames =
            context_->stack_profile_tracker->JavaFramesForName(
                {*merged_obfuscated_id, *opt_package_name_id});
        frames.insert(frames.end(), pkg_frames.begin(), pkg_frames.end());
      }
      if (opt_memfd_id) {
        const std::vector<tables::StackProfileFrameTable::Id> memfd_frames =
            context_->stack_profile_tracker->JavaFramesForName(
                {*merged_obfuscated_id, *opt_memfd_id});
        frames.insert(frames.end(), memfd_frames.begin(), memfd_frames.end());
      }

      for (tables::StackProfileFrameTable::Id frame_id : frames) {
        auto* frames_tbl =
            context_->storage->mutable_stack_profile_frame_table();
        auto rr = *frames_tbl->FindById(frame_id);
        rr.set_deobfuscated_name(context_->storage->InternString(
            base::StringView(merged_deobfuscated)));
      }
      obfuscated_to_deobfuscated_members[context_->storage->InternString(
          member.obfuscated_name())] =
          context_->storage->InternString(member.deobfuscated_name());
    }
    // Members can contain a class name (e.g "ClassA.FunctionF")
    deobfuscation_mapping_table.AddClassTranslation(
        DeobfuscationMappingTable::PackageId{
            deobfuscation_mapping.package_name().ToStdString(),
            deobfuscation_mapping.version_code()},
        context_->storage->InternString(cls.obfuscated_name()),
        context_->storage->InternString(cls.deobfuscated_name()),
        std::move(obfuscated_to_deobfuscated_members));
  }
  context_->args_translation_table->AddDeobfuscationMappingTable(
      std::move(deobfuscation_mapping_table));
}

void DeobfuscationModule::GuessPackageForCallsite(
    tables::ProcessTable::Id upid,
    tables::StackProfileCallsiteTable::Id callsite_id) {
  const auto& process_table = context_->storage->process_table();
  auto* stack_profile_tracker = context_->stack_profile_tracker.get();

  auto process = process_table.FindById(upid);
  if (!process.has_value()) {
    return;
  }

  if (!process->android_appid().has_value()) {
    return;
  }

  std::optional<StringId> package;

  for (auto it = context_->storage->package_list_table().IterateRows(); it;
       ++it) {
    if (it.uid() == *process->android_appid()) {
      package = it.package_name();
      break;
    }
  }

  if (!package.has_value()) {
    return;
  }

  const auto& callsite_table =
      context_->storage->stack_profile_callsite_table();
  auto callsite = callsite_table.FindById(callsite_id);
  while (callsite.has_value()) {
    auto frame_id = callsite->frame_id();

    if (stack_profile_tracker->FrameHasUnknownPackage(frame_id) != 0) {
      if (process->name().has_value()) {
        stack_profile_tracker->SetPackageForFrame(*package, frame_id);
      }
    }

    auto parent_id = callsite->parent_id();
    callsite.reset();
    if (parent_id.has_value()) {
      callsite = callsite_table.FindById(*parent_id);
    }
  }
}

void DeobfuscationModule::GuessPackages() {
  const auto& heap_profile_allocation_table =
      context_->storage->heap_profile_allocation_table();
  for (auto allocation = heap_profile_allocation_table.IterateRows();
       allocation; ++allocation) {
    auto upid = tables::ProcessTable::Id(allocation.upid());
    auto callsite_id = allocation.callsite_id();

    GuessPackageForCallsite(upid, callsite_id);
  }

  const auto& perf_sample_table = context_->storage->perf_sample_table();
  for (auto sample = perf_sample_table.IterateRows(); sample; ++sample) {
    auto thread = context_->storage->thread_table().FindById(
        tables::ThreadTable::Id(sample.utid()));
    if (!thread || !thread->upid().has_value() ||
        !sample.callsite_id().has_value()) {
      continue;
    }
    GuessPackageForCallsite(tables::ProcessTable::Id(*thread->upid()),
                            *sample.callsite_id());
  }
}

void DeobfuscationModule::NotifyEndOfFile() {
  auto* heap_graph_tracker = HeapGraphTracker::GetOrCreate(context_);
  heap_graph_tracker->FinalizeAllProfiles();

  if (context_->stack_profile_tracker->HasFramesWithoutKnownPackage()) {
    GuessPackages();
  }

  for (const auto& packet : packets_) {
    ParseDeobfuscationMapping(ConstBytes{packet.data(), packet.size()},
                              heap_graph_tracker);
  }
}

}  // namespace perfetto::trace_processor
