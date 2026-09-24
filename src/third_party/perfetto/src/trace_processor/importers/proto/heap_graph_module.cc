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

#include "src/trace_processor/importers/proto/heap_graph_module.h"
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_utils.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/importers/proto/heap_graph_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "perfetto/ext/base/string_view.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/third_party/android/art/heap_graph.pbzero.h"

namespace perfetto::trace_processor {

using ::com::android::art::tracing::pbzero::HeapGraph;
using ::com::android::art::tracing::pbzero::HeapGraphObject;
using ::com::android::art::tracing::pbzero::HeapGraphRoot;
using ::com::android::art::tracing::pbzero::HeapGraphRoot_Type_MAX;
using ::com::android::art::tracing::pbzero::HeapGraphRoot_Type_MIN;
using ::com::android::art::tracing::pbzero::HeapGraphType;
using ::com::android::art::tracing::pbzero::HeapGraphType_Kind_MAX;
using ::com::android::art::tracing::pbzero::HeapGraphType_Kind_MIN;

namespace {

using ClassTable = tables::HeapGraphClassTable;
using ObjectTable = tables::HeapGraphObjectTable;
using ReferenceTable = tables::HeapGraphReferenceTable;

// Iterate over a repeated field of varints, independent of whether it is
// packed or not.
template <int32_t field_no, typename T, typename F>
bool ForEachVarInt(const T& decoder, F fn) {
  auto field = decoder.template at<field_no>();
  bool parse_error = false;
  if (field.type() == protozero::proto_utils::ProtoWireType::kLengthDelimited) {
    // packed repeated
    auto it = decoder.template GetPackedRepeated<
        ::protozero::proto_utils::ProtoWireType::kVarInt, uint64_t>(
        field_no, &parse_error);
    for (; it; ++it)
      fn(*it);
  } else {
    // non-packed repeated
    auto it = decoder.template GetRepeated<uint64_t>(field_no);
    for (; it; ++it)
      fn(*it);
  }
  return parse_error;
}

}  // namespace

using com::android::art::tracing::pbzero::ArtHeapGraphTracePacket;
using perfetto::protos::pbzero::TracePacket;

HeapGraphModule::HeapGraphModule(ProtoImporterModuleContext* module_context,
                                 TraceProcessorContext* context)
    : ProtoImporterModule(module_context), context_(context) {
  RegisterForField(ArtHeapGraphTracePacket::kHeapGraphFieldNumber);
}

void HeapGraphModule::ParseField(const ParseFieldArgs& args) {
  switch (args.field.id()) {
    case ArtHeapGraphTracePacket::kHeapGraphFieldNumber:
      ParseHeapGraph(args.decoder.trusted_packet_sequence_id(), args.ts,
                     args.field.Cast<ArtHeapGraphTracePacket::kHeapGraph>());
      return;
    default:
      break;
  }
}

void HeapGraphModule::ParseHeapGraph(uint32_t seq_id,
                                     int64_t ts,
                                     protozero::ConstBytes blob) {
  auto* heap_graph_tracker = HeapGraphTracker::Get(context_);
  HeapGraph::Decoder heap_graph(blob.data, blob.size);
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(
      static_cast<uint32_t>(heap_graph.pid()));
  heap_graph_tracker->SetPacketIndex(seq_id, heap_graph.index());
  if (heap_graph.has_heap_bytes_allocated()) {
    heap_graph_tracker->SetHeapSize(seq_id, heap_graph.heap_bytes_allocated());
  }
  for (auto it = heap_graph.objects(); it; ++it) {
    HeapGraphObject::Decoder object(*it);
    HeapGraphTracker::SourceObject obj;
    if (object.id_delta()) {
      obj.object_id =
          heap_graph_tracker->GetLastObjectId(seq_id) + object.id_delta();
    } else {
      obj.object_id = object.id();
    }
    obj.self_size = object.self_size();
    obj.type_id = object.type_id();
    obj.heap_type = object.has_heap_type_delta()
                        ? HeapGraphObject::HeapType(object.heap_type_delta())
                        : heap_graph_tracker->GetLastObjectHeapType(seq_id);

    // Even though the field is named reference_field_id_base, it has always
    // been used as a base for reference_object_id.
    uint64_t base_obj_id = object.reference_field_id_base();

    // In S+ traces, this field will not be set for normal instances. It will be
    // set in the corresponding HeapGraphType instead. It will still be set for
    // class objects.
    //
    // grep-friendly: reference_field_id
    bool parse_error =
        ForEachVarInt<HeapGraphObject::kReferenceFieldIdFieldNumber>(
            object,
            [&obj](uint64_t value) { obj.field_name_ids.push_back(value); });

    if (!parse_error) {
      // grep-friendly: reference_object_id
      parse_error =
          ForEachVarInt<HeapGraphObject::kReferenceObjectIdFieldNumber>(
              object, [&obj, base_obj_id](uint64_t value) {
                if (value)
                  value += base_obj_id;
                obj.referred_objects.push_back(value);
              });
    }

    if (!parse_error) {
      // grep-friendly: runtime_internal_object_id
      parse_error =
          ForEachVarInt<HeapGraphObject::kRuntimeInternalObjectIdFieldNumber>(
              object, [&obj](uint64_t value) {
                obj.runtime_internal_objects.push_back(value);
              });
    }

    if (object.has_native_allocation_registry_size_field()) {
      obj.native_allocation_registry_size =
          object.native_allocation_registry_size_field();
    }

    if (object.has_bitmap_id_field()) {
      obj.bitmap_id = object.bitmap_id_field();
    }
    if (object.has_bitmap_source_id_field()) {
      obj.bitmap_source_id = object.bitmap_source_id_field();
    }
    if (object.has_bitmap_width_field()) {
      obj.bitmap_width = object.bitmap_width_field();
    }
    if (object.has_bitmap_height_field()) {
      obj.bitmap_height = object.bitmap_height_field();
    }

    if (parse_error) {
      context_->stats_tracker->IncrementIndexedStats(
          stats::heap_graph_malformed_packet, static_cast<int>(upid));
      break;
    }
    if (!obj.field_name_ids.empty() &&
        (obj.field_name_ids.size() != obj.referred_objects.size())) {
      context_->stats_tracker->IncrementIndexedStats(
          stats::heap_graph_malformed_packet, static_cast<int>(upid));
      continue;
    }
    heap_graph_tracker->AddObject(seq_id, upid, ts, std::move(obj));
  }
  for (auto it = heap_graph.types(); it; ++it) {
    std::vector<uint64_t> field_name_ids;
    HeapGraphType::Decoder entry(*it);
    const char* str = reinterpret_cast<const char*>(entry.class_name().data);
    auto str_view = base::StringView(str, entry.class_name().size);

    // grep-friendly: reference_field_id
    bool parse_error =
        ForEachVarInt<HeapGraphType::kReferenceFieldIdFieldNumber>(
            entry, [&field_name_ids](uint64_t value) {
              field_name_ids.push_back(value);
            });

    if (parse_error) {
      context_->stats_tracker->IncrementIndexedStats(
          stats::heap_graph_malformed_packet, static_cast<int>(upid));
      continue;
    }

    bool no_fields = entry.kind() == HeapGraphType::KIND_NOREFERENCES ||
                     entry.kind() == HeapGraphType::KIND_ARRAY ||
                     entry.kind() == HeapGraphType::KIND_STRING;

    HeapGraphType::Kind kind = HeapGraphType::KIND_UNKNOWN;
    if (HeapGraphType_Kind_MIN <= entry.kind() &&
        entry.kind() <= HeapGraphType_Kind_MAX) {
      kind = HeapGraphType::Kind(entry.kind());
    }

    std::optional<uint64_t> location_id;
    if (entry.has_location_id())
      location_id = entry.location_id();

    heap_graph_tracker->AddInternedType(
        seq_id, entry.id(), context_->storage->InternString(str_view),
        location_id, entry.object_size(), std::move(field_name_ids),
        entry.superclass_id(), entry.classloader_id(), no_fields, kind);
  }
  for (auto it = heap_graph.field_names(); it; ++it) {
    protos::pbzero::InternedString::Decoder entry(*it);
    const char* str = reinterpret_cast<const char*>(entry.str().data);
    auto str_view = base::StringView(str, entry.str().size);

    heap_graph_tracker->AddInternedFieldName(seq_id, entry.iid(), str_view);
  }
  for (auto it = heap_graph.location_names(); it; ++it) {
    protos::pbzero::InternedString::Decoder entry(*it);
    const char* str = reinterpret_cast<const char*>(entry.str().data);
    auto str_view = base::StringView(str, entry.str().size);

    heap_graph_tracker->AddInternedLocationName(
        seq_id, entry.iid(), context_->storage->InternString(str_view));
  }
  for (auto it = heap_graph.roots(); it; ++it) {
    HeapGraphRoot::Decoder entry(*it);

    HeapGraphTracker::SourceRoot src_root;
    if (HeapGraphRoot_Type_MIN <= entry.root_type() &&
        entry.root_type() <= HeapGraphRoot_Type_MAX) {
      src_root.root_type = HeapGraphRoot::Type(entry.root_type());
    } else {
      src_root.root_type = HeapGraphRoot::ROOT_UNKNOWN;
    }
    // grep-friendly: object_ids
    bool parse_error = ForEachVarInt<HeapGraphRoot::kObjectIdsFieldNumber>(
        entry, [&src_root](uint64_t value) {
          src_root.object_ids.emplace_back(value);
        });
    if (parse_error) {
      context_->stats_tracker->IncrementIndexedStats(
          stats::heap_graph_malformed_packet, static_cast<int>(upid));
      break;
    }
    heap_graph_tracker->AddRoot(seq_id, upid, ts, std::move(src_root));
  }
  if (!heap_graph.continued()) {
    heap_graph_tracker->FinalizeProfile(seq_id);
  }
}

void HeapGraphModule::OnEventsFullyExtracted() {
  auto* heap_graph_tracker = HeapGraphTracker::Get(context_);
  heap_graph_tracker->FinalizeAllProfiles();
}

}  // namespace perfetto::trace_processor
