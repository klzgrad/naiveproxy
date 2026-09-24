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

#include "src/trace_processor/importers/proto/heap_graph_tracker.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "protos/third_party/android/art/heap_graph.pbzero.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/importers/common/global_stats_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/util/profiler_util.h"

namespace perfetto::trace_processor {

namespace {

using ClassTable = tables::HeapGraphClassTable;
using FlamegraphId = tables::ExperimentalFlamegraphTable_Id;
using ObjectTable = tables::HeapGraphObjectTable;
using ReferenceTable = tables::HeapGraphReferenceTable;

// Iterates all the references owned by the object `id`.
//
// Calls bool(*fn)(ObjectTable::RowReference) with the each row
// from the `storage.heap_graph_reference()` table associated to the |object|.
// When `fn` returns false (or when there are no more rows owned by |object|),
// stops the iteration.
template <typename F>
void ForReferenceSet(tables::HeapGraphReferenceTable::Cursor& cursor,
                     std::optional<uint32_t> reference_set_id,
                     F fn) {
  if (!reference_set_id) {
    return;
  }
  cursor.SetFilterValueUnchecked(0, *reference_set_id);
  for (cursor.Execute(); !cursor.Eof(); cursor.Next()) {
    if (!fn(cursor)) {
      break;
    }
  }
}

struct ClassDescriptor {
  StringId name;
  std::optional<StringId> location;

  bool operator<(const ClassDescriptor& other) const {
    return std::tie(name, location) < std::tie(other.name, other.location);
  }
};

ClassDescriptor GetClassDescriptor(const TraceStorage& storage,
                                   ObjectTable::Id obj_id) {
  auto obj_row_ref = storage.heap_graph_object_table()[obj_id];
  auto type_row_ref = storage.heap_graph_class_table()[obj_row_ref.type_id()];
  return {type_row_ref.name(), type_row_ref.location()};
}

std::optional<ObjectTable::Id> GetReferredObj(
    tables::HeapGraphReferenceTable::Cursor& referred_cursor,
    uint32_t ref_set_id,
    const std::string& field_name) {
  referred_cursor.SetFilterValueUnchecked(0, ref_set_id);
  referred_cursor.SetFilterValueUnchecked(1, field_name.c_str());
  referred_cursor.Execute();
  if (referred_cursor.Eof()) {
    return std::nullopt;
  }
  return referred_cursor.owned_id();
}

// Maps from normalized class name and location, to superclass.
std::map<ClassDescriptor, ClassDescriptor> BuildSuperclassMap(
    UniquePid upid,
    int64_t ts,
    TraceStorage* storage,
    tables::HeapGraphObjectTable::Cursor& superclass_cursor,
    tables::HeapGraphReferenceTable::Cursor& referred_cursor) {
  std::map<ClassDescriptor, ClassDescriptor> superclass_map;

  // Resolve superclasses by iterating heap graph objects and identifying the
  // superClass field.
  superclass_cursor.SetFilterValueUnchecked(0, upid);
  superclass_cursor.SetFilterValueUnchecked(1, ts);
  superclass_cursor.Execute();
  for (; !superclass_cursor.Eof(); superclass_cursor.Next()) {
    auto obj_id = superclass_cursor.id();
    auto class_descriptor = GetClassDescriptor(*storage, obj_id);
    auto normalized =
        GetNormalizedType(storage->GetString(class_descriptor.name));
    // superClass ptrs are stored on the static class objects
    // ignore arrays (as they are generated objects)
    if (!normalized.is_static_class || normalized.number_of_arrays > 0) {
      continue;
    }

    auto opt_ref_set_id = superclass_cursor.reference_set_id();
    if (!opt_ref_set_id) {
      continue;
    }
    auto super_obj_id = GetReferredObj(referred_cursor, *opt_ref_set_id,
                                       "java.lang.Class.superClass");
    if (!super_obj_id) {
      // This is expected to be missing for Object and primitive types
      continue;
    }

    // Lookup the super obj type id
    auto super_class_descriptor = GetClassDescriptor(*storage, *super_obj_id);
    auto super_class_name =
        NormalizeTypeName(storage->GetString(super_class_descriptor.name));
    StringId super_class_id = storage->InternString(super_class_name);
    StringId class_id = storage->InternString(normalized.name);
    superclass_map[{class_id, class_descriptor.location}] = {
        super_class_id, super_class_descriptor.location};
  }
  return superclass_map;
}

// Extract the size from `nar_size`, which is the value of a
// libcore.util.NativeAllocationRegistry.size field: it encodes the size, but
// uses the least significant bit to represent the source of the allocation.
int64_t GetSizeFromNativeAllocationRegistry(int64_t nar_size) {
  constexpr uint64_t kIsMalloced = 1;
  return static_cast<int64_t>(static_cast<uint64_t>(nar_size) & ~kIsMalloced);
}

// A given object can be a heap root in different ways. Ensure analysis is
// consistent.
constexpr std::array<::com::android::art::tracing::pbzero::HeapGraphRoot::Type,
                     3>
    kRootTypePrecedence = {
        ::com::android::art::tracing::pbzero::HeapGraphRoot::ROOT_STICKY_CLASS,
        ::com::android::art::tracing::pbzero::HeapGraphRoot::ROOT_JNI_GLOBAL,
        ::com::android::art::tracing::pbzero::HeapGraphRoot::ROOT_JNI_LOCAL,
};

}  // namespace

std::optional<base::StringView> GetStaticClassTypeName(base::StringView type) {
  static const base::StringView kJavaClassTemplate("java.lang.Class<");
  if (!type.empty() && type.at(type.size() - 1) == '>' &&
      type.substr(0, kJavaClassTemplate.size()) == kJavaClassTemplate) {
    return type.substr(kJavaClassTemplate.size(),
                       type.size() - kJavaClassTemplate.size() - 1);
  }
  return {};
}

size_t NumberOfArrays(base::StringView type) {
  if (type.size() < 2)
    return 0;

  size_t arrays = 0;
  while (type.size() >= 2 * (arrays + 1) &&
         memcmp(type.end() - (2 * (arrays + 1)), "[]", 2) == 0) {
    arrays++;
  }
  return arrays;
}

NormalizedType GetNormalizedType(base::StringView type) {
  auto static_class_type_name = GetStaticClassTypeName(type);
  if (static_class_type_name.has_value()) {
    type = static_class_type_name.value();
  }
  size_t number_of_arrays = NumberOfArrays(type);
  return {base::StringView(type.data(), type.size() - (number_of_arrays * 2)),
          static_class_type_name.has_value(), number_of_arrays};
}

base::StringView NormalizeTypeName(base::StringView type) {
  return GetNormalizedType(type).name;
}

std::string DenormalizeTypeName(NormalizedType normalized,
                                base::StringView deobfuscated_type_name) {
  std::string result = deobfuscated_type_name.ToStdString();
  for (size_t i = 0; i < normalized.number_of_arrays; ++i) {
    result += "[]";
  }
  if (normalized.is_static_class) {
    result = "java.lang.Class<" + result + ">";
  }
  return result;
}

HeapGraphTracker::HeapGraphTracker(TraceStorage* storage,
                                   GlobalStatsTracker* stats_tracker)
    : storage_(storage),
      global_stats_tracker_(stats_tracker),
      class_cursor_(storage->mutable_heap_graph_class_table()->CreateCursor({
          dataframe::FilterSpec{
              tables::HeapGraphClassTable::ColumnIndex::name,
              0,
              dataframe::Eq{},
              {},
          },
      })),
      object_cursor_(storage->mutable_heap_graph_object_table()->CreateCursor({
          dataframe::FilterSpec{
              tables::HeapGraphObjectTable::ColumnIndex::type_id,
              0,
              dataframe::Eq{},
              {},
          },
          dataframe::FilterSpec{
              tables::HeapGraphObjectTable::ColumnIndex::upid,
              1,
              dataframe::Eq{},
              {},
          },
          dataframe::FilterSpec{
              tables::HeapGraphObjectTable::ColumnIndex::graph_sample_ts,
              2,
              dataframe::Eq{},
              {},
          },
      })),
      superclass_cursor_(
          storage->mutable_heap_graph_object_table()->CreateCursor({
              dataframe::FilterSpec{
                  tables::HeapGraphObjectTable::ColumnIndex::upid,
                  0,
                  dataframe::Eq{},
                  {},
              },
              dataframe::FilterSpec{
                  tables::HeapGraphObjectTable::ColumnIndex::graph_sample_ts,
                  1,
                  dataframe::Eq{},
                  {},
              },
          })),
      reference_cursor_(
          storage->mutable_heap_graph_reference_table()->CreateCursor({
              dataframe::FilterSpec{
                  tables::HeapGraphReferenceTable::ColumnIndex::
                      reference_set_id,
                  0,
                  dataframe::Eq{},
                  {},
              },
          })),
      referred_cursor_(
          storage->mutable_heap_graph_reference_table()->CreateCursor({
              dataframe::FilterSpec{
                  tables::HeapGraphReferenceTable::ColumnIndex::
                      reference_set_id,
                  0,
                  dataframe::Eq{},
                  {},
              },
              dataframe::FilterSpec{
                  tables::HeapGraphReferenceTable::ColumnIndex::field_name,
                  1,
                  dataframe::Eq{},
                  {},
              },
          })),
      heap_graph_cursor_(storage->mutable_heap_graph_table()->CreateCursor({
          dataframe::FilterSpec{
              tables::HeapGraphTable::ColumnIndex::upid,
              0,
              dataframe::Eq{},
              {},
          },
          dataframe::FilterSpec{
              tables::HeapGraphTable::ColumnIndex::ts,
              1,
              dataframe::Eq{},
              {},
          },
      })),
      cleaner_thunk_str_id_(storage_->InternString("sun.misc.Cleaner.thunk")),
      referent_str_id_(
          storage_->InternString("java.lang.ref.Reference.referent")),
      cleaner_thunk_this0_str_id_(storage_->InternString(
          "libcore.util.NativeAllocationRegistry$CleanerThunk.this$0")),
      native_size_str_id_(
          storage_->InternString("libcore.util.NativeAllocationRegistry.size")),
      cleaner_next_str_id_(storage_->InternString("sun.misc.Cleaner.next")) {
  for (size_t i = 0; i < root_type_string_ids_.size(); i++) {
    auto val =
        static_cast<::com::android::art::tracing::pbzero::HeapGraphRoot::Type>(
            i);
    auto str_view = base::StringView(
        ::com::android::art::tracing::pbzero::HeapGraphRoot_Type_Name(val));
    root_type_string_ids_[i] = storage_->InternString(str_view);
  }

  for (size_t i = 0; i < type_kind_string_ids_.size(); i++) {
    auto val =
        static_cast<::com::android::art::tracing::pbzero::HeapGraphType::Kind>(
            i);
    auto str_view = base::StringView(
        ::com::android::art::tracing::pbzero::HeapGraphType_Kind_Name(val));
    type_kind_string_ids_[i] = storage_->InternString(str_view);
  }
}

HeapGraphTracker::SequenceState& HeapGraphTracker::GetOrCreateSequence(
    uint32_t seq_id) {
  return sequence_state_[seq_id];
}

bool HeapGraphTracker::SetPidAndTimestamp(SequenceState* sequence_state,
                                          UniquePid upid,
                                          int64_t ts) {
  if (sequence_state->current_upid != 0 &&
      sequence_state->current_upid != upid) {
    global_stats_tracker_->IncrementGlobalStats(
        stats::heap_graph_non_finalized_graph);
    return false;
  }
  if (sequence_state->current_ts != 0 && sequence_state->current_ts != ts) {
    global_stats_tracker_->IncrementGlobalStats(
        stats::heap_graph_non_finalized_graph);
    return false;
  }
  sequence_state->current_upid = upid;
  sequence_state->current_ts = ts;
  return true;
}

ObjectTable::RowReference HeapGraphTracker::GetOrInsertObject(
    SequenceState* sequence_state,
    uint64_t object_id) {
  auto* object_table = storage_->mutable_heap_graph_object_table();
  auto* ptr = sequence_state->object_id_to_db_row.Find(object_id);
  if (!ptr) {
    auto id_and_row = object_table->Insert({sequence_state->current_upid,
                                            sequence_state->current_ts,
                                            -1,
                                            0,
                                            /*reference_set_id=*/std::nullopt,
                                            /*reachable=*/0,
                                            /*heap_type=*/std::nullopt,
                                            {},
                                            /*root_type=*/std::nullopt,
                                            /*root_distance*/ -1});
    bool inserted;
    std::tie(ptr, inserted) = sequence_state->object_id_to_db_row.Insert(
        object_id, id_and_row.row_number);
  }
  return ptr->ToRowReference(object_table);
}

ClassTable::RowReference HeapGraphTracker::GetOrInsertType(
    SequenceState* sequence_state,
    uint64_t type_id) {
  auto* class_table = storage_->mutable_heap_graph_class_table();
  auto* ptr = sequence_state->type_id_to_db_row.Find(type_id);
  if (!ptr) {
    auto id_and_row =
        class_table->Insert({StringId(), std::nullopt, std::nullopt});
    bool inserted;
    std::tie(ptr, inserted) = sequence_state->type_id_to_db_row.Insert(
        type_id, id_and_row.row_number);
  }
  return ptr->ToRowReference(class_table);
}

void HeapGraphTracker::AddObject(uint32_t seq_id,
                                 UniquePid upid,
                                 int64_t ts,
                                 SourceObject obj) {
  SequenceState& sequence_state = GetOrCreateSequence(seq_id);

  if (!SetPidAndTimestamp(&sequence_state, upid, ts))
    return;

  sequence_state.last_object_id = obj.object_id;
  sequence_state.last_heap_type = obj.heap_type;

  ObjectTable::RowReference owner_row_ref =
      GetOrInsertObject(&sequence_state, obj.object_id);
  ClassTable::RowReference type_row_ref =
      GetOrInsertType(&sequence_state, obj.type_id);

  ClassTable::Id type_id = type_row_ref.id();

  owner_row_ref.set_self_size(static_cast<int64_t>(obj.self_size));
  owner_row_ref.set_type_id(type_id);
  if (obj.heap_type != ::com::android::art::tracing::pbzero::HeapGraphObject::
                           HEAP_TYPE_UNKNOWN) {
    owner_row_ref.set_heap_type(storage_->InternString(base::StringView(
        ::com::android::art::tracing::pbzero::HeapGraphObject_HeapType_Name(
            obj.heap_type))));
    if (obj.heap_type == ::com::android::art::tracing::pbzero::HeapGraphObject::
                             HEAP_TYPE_ZYGOTE ||
        obj.heap_type == ::com::android::art::tracing::pbzero::HeapGraphObject::
                             HEAP_TYPE_BOOT_IMAGE) {
      // The ART GC doesn't collect these objects:
      // https://cs.android.com/android/platform/superproject/main/+/main:art/runtime/gc/collector/mark_compact.cc;l=682;drc=6484611fd45e69db9f33f98bfd6864014b030ecf
      // Let's mark them as roots.
      sequence_state.internal_vm_roots.emplace_back(obj.object_id);
    }
  }

  if (obj.self_size == 0) {
    sequence_state.deferred_size_objects_for_type_[type_id].push_back(
        owner_row_ref.ToRowNumber());
  }

  uint32_t reference_set_id =
      storage_->heap_graph_reference_table().row_count();
  bool any_references = false;
  bool any_native_references = false;

  ObjectTable::Id owner_id = owner_row_ref.id();
  for (size_t i = 0; i < obj.referred_objects.size(); ++i) {
    uint64_t owned_object_id = obj.referred_objects[i];
    // This is true for unset reference fields.
    std::optional<ObjectTable::RowReference> owned_row_ref;
    if (owned_object_id != 0)
      owned_row_ref = GetOrInsertObject(&sequence_state, owned_object_id);

    auto ref_id_and_row =
        storage_->mutable_heap_graph_reference_table()->Insert(
            {reference_set_id,
             owner_id,
             owned_row_ref ? std::make_optional(owned_row_ref->id())
                           : std::nullopt,
             {},
             {},
             /*deobfuscated_field_name=*/std::nullopt});
    if (!obj.field_name_ids.empty()) {
      sequence_state.references_for_field_name_id[obj.field_name_ids[i]]
          .push_back(ref_id_and_row.row_number);
    }
    any_references = true;
  }
  for (size_t i = 0; i < obj.runtime_internal_objects.size(); ++i) {
    uint64_t owned_object_id = obj.runtime_internal_objects[i];
    // This is true for unset reference fields.
    ObjectTable::RowReference owned_row_ref =
        GetOrInsertObject(&sequence_state, owned_object_id);

    storage_->mutable_heap_graph_reference_table()->Insert(
        {reference_set_id,
         owner_id,
         std::make_optional(owned_row_ref.id()),
         storage_->InternString("runtimeInternalObjects"),
         {},
         /*deobfuscated_field_name=*/std::nullopt});
    any_native_references = true;
  }
  if (any_references || any_native_references) {
    owner_row_ref.set_reference_set_id(reference_set_id);
  }
  if (any_references) {
    if (obj.field_name_ids.empty()) {
      sequence_state.deferred_reference_objects_for_type_[type_id].push_back(
          owner_row_ref.ToRowNumber());
    }
  }

  if (obj.native_allocation_registry_size.has_value()) {
    sequence_state.nar_size_by_obj_id[owner_id] =
        *obj.native_allocation_registry_size;
  }

  bool has_bitmap_fields =
      obj.bitmap_id.has_value() || obj.bitmap_source_id.has_value() ||
      obj.bitmap_width.has_value() || obj.bitmap_height.has_value();

  if (has_bitmap_fields) {
    auto* prim_table = storage_->mutable_heap_graph_primitive_table();
    uint32_t field_set_id = prim_table->row_count();

    auto insert_primitive =
        [&](const char* name, const char* type, auto value,
            std::optional<int64_t> tables::HeapGraphPrimitiveTable::Row::*
                column) {
          if (value.has_value()) {
            tables::HeapGraphPrimitiveTable::Row row;
            row.field_set_id = field_set_id;
            row.field_name = storage_->InternString(name);
            row.field_type = storage_->InternString(type);
            row.*column = static_cast<int64_t>(*value);
            prim_table->Insert(row);
          }
        };

    insert_primitive("android.graphics.Bitmap.mId", "long", obj.bitmap_id,
                     &tables::HeapGraphPrimitiveTable::Row::long_value);
    insert_primitive("android.graphics.Bitmap.mSourceId", "long",
                     obj.bitmap_source_id,
                     &tables::HeapGraphPrimitiveTable::Row::long_value);
    insert_primitive("android.graphics.Bitmap.mWidth", "int", obj.bitmap_width,
                     &tables::HeapGraphPrimitiveTable::Row::int_value);
    insert_primitive("android.graphics.Bitmap.mHeight", "int",
                     obj.bitmap_height,
                     &tables::HeapGraphPrimitiveTable::Row::int_value);

    // Link to object data
    auto* data_table = storage_->mutable_heap_graph_object_data_table();
    tables::HeapGraphObjectDataTable::Row data_row;
    data_row.field_set_id = field_set_id;
    auto data_id = data_table->Insert(data_row).id;

    owner_row_ref.set_object_data_id(data_id.value);
  }
}

void HeapGraphTracker::AddRoot(uint32_t seq_id,
                               UniquePid upid,
                               int64_t ts,
                               SourceRoot root) {
  SequenceState& sequence_state = GetOrCreateSequence(seq_id);
  if (!SetPidAndTimestamp(&sequence_state, upid, ts))
    return;

  sequence_state.current_roots.emplace_back(std::move(root));
}

void HeapGraphTracker::AddInternedLocationName(uint32_t seq_id,
                                               uint64_t intern_id,
                                               StringId strid) {
  SequenceState& sequence_state = GetOrCreateSequence(seq_id);
  sequence_state.interned_location_names.emplace(intern_id, strid);
}

void HeapGraphTracker::AddInternedType(
    uint32_t seq_id,
    uint64_t intern_id,
    StringId strid,
    std::optional<uint64_t> location_id,
    uint64_t object_size,
    std::vector<uint64_t> field_name_ids,
    uint64_t superclass_id,
    uint64_t classloader_id,
    bool no_fields,
    ::com::android::art::tracing::pbzero::HeapGraphType::Kind kind) {
  SequenceState& sequence_state = GetOrCreateSequence(seq_id);
  InternedType& type = sequence_state.interned_types[intern_id];
  type.name = strid;
  type.location_id = location_id;
  type.object_size = object_size;
  type.field_name_ids = std::move(field_name_ids);
  type.superclass_id = superclass_id;
  type.classloader_id = classloader_id;
  type.no_fields = no_fields;
  type.kind = kind;
}

void HeapGraphTracker::AddInternedFieldName(uint32_t seq_id,
                                            uint64_t intern_id,
                                            base::StringView str) {
  SequenceState& sequence_state = GetOrCreateSequence(seq_id);
  size_t space = str.find(' ');
  base::StringView type;
  if (space != base::StringView::npos) {
    type = str.substr(0, space);
    str = str.substr(space + 1);
  }
  StringId field_name = storage_->InternString(str);
  StringId type_name = storage_->InternString(type);

  sequence_state.interned_fields.Insert(intern_id,
                                        InternedField{field_name, type_name});

  auto it = sequence_state.references_for_field_name_id.find(intern_id);
  if (it != sequence_state.references_for_field_name_id.end()) {
    auto* hgr = storage_->mutable_heap_graph_reference_table();
    for (ReferenceTable::RowNumber reference_row_num : it->second) {
      auto row_ref = reference_row_num.ToRowReference(hgr);
      row_ref.set_field_name(field_name);
      row_ref.set_field_type_name(type_name);
      field_to_rows_[field_name].emplace_back(reference_row_num);
    }
  }
}

void HeapGraphTracker::SetPacketIndex(uint32_t seq_id, uint64_t index) {
  SequenceState& sequence_state = GetOrCreateSequence(seq_id);
  bool dropped_packet = false;
  // perfetto_hprof starts counting at index = 0.
  if (!sequence_state.prev_index && index != 0) {
    dropped_packet = true;
  }

  if (sequence_state.prev_index && *sequence_state.prev_index + 1 != index) {
    dropped_packet = true;
  }

  if (dropped_packet) {
    sequence_state.truncated = true;
    if (sequence_state.prev_index) {
      PERFETTO_ELOG("Missing packets between %" PRIu64 " and %" PRIu64,
                    *sequence_state.prev_index, index);
    } else {
      PERFETTO_ELOG("Invalid first packet index %" PRIu64 " (!= 0)", index);
    }

    global_stats_tracker_->IncrementGlobalIndexedStats(
        stats::heap_graph_missing_packet,
        static_cast<int>(sequence_state.current_upid));
  }
  sequence_state.prev_index = index;
}

void HeapGraphTracker::SetHeapSize(uint32_t seq_id, int64_t heap_size) {
  GetOrCreateSequence(seq_id).heap_size = heap_size;
}

// This only works on Android S+ traces. We need to have ingested the whole
// profile before calling this function (e.g. in FinalizeProfile).
HeapGraphTracker::InternedType* HeapGraphTracker::GetSuperClass(
    SequenceState* sequence_state,
    const InternedType* current_type) {
  if (current_type->superclass_id) {
    auto it = sequence_state->interned_types.find(current_type->superclass_id);
    if (it != sequence_state->interned_types.end())
      return &it->second;
  }
  global_stats_tracker_->IncrementGlobalIndexedStats(
      stats::heap_graph_malformed_packet,
      static_cast<int>(sequence_state->current_upid));
  return nullptr;
}

void HeapGraphTracker::FinalizeProfile(uint32_t seq_id) {
  SequenceState& sequence_state = GetOrCreateSequence(seq_id);

  // We do this in FinalizeProfile because the interned_location_names get
  // written at the end of the dump.
  for (const auto& p : sequence_state.interned_types) {
    uint64_t id = p.first;
    const InternedType& interned_type = p.second;
    std::optional<StringId> location_name;
    if (interned_type.location_id) {
      auto it = sequence_state.interned_location_names.find(
          *interned_type.location_id);
      if (it == sequence_state.interned_location_names.end()) {
        global_stats_tracker_->IncrementGlobalIndexedStats(
            stats::heap_graph_invalid_string_id,
            static_cast<int>(sequence_state.current_upid));
      } else {
        location_name = it->second;
      }
    }
    ClassTable::RowReference type_row_ref =
        GetOrInsertType(&sequence_state, id);
    ClassTable::Id type_id = type_row_ref.id();

    auto sz_obj_it =
        sequence_state.deferred_size_objects_for_type_.find(type_id);
    if (sz_obj_it != sequence_state.deferred_size_objects_for_type_.end()) {
      auto* hgo = storage_->mutable_heap_graph_object_table();
      for (ObjectTable::RowNumber obj_row_num : sz_obj_it->second) {
        auto obj_row_ref = obj_row_num.ToRowReference(hgo);
        obj_row_ref.set_self_size(
            static_cast<int64_t>(interned_type.object_size));
      }
      sequence_state.deferred_size_objects_for_type_.erase(sz_obj_it);
    }

    auto ref_obj_it =
        sequence_state.deferred_reference_objects_for_type_.find(type_id);
    if (ref_obj_it !=
        sequence_state.deferred_reference_objects_for_type_.end()) {
      for (ObjectTable::RowNumber obj_row_number : ref_obj_it->second) {
        auto obj_row_ref = obj_row_number.ToRowReference(
            storage_->mutable_heap_graph_object_table());
        const InternedType* current_type = &interned_type;
        if (interned_type.no_fields) {
          continue;
        }
        size_t field_offset_in_cls = 0;
        ForReferenceSet(
            reference_cursor_, obj_row_ref.reference_set_id(),
            [this, &current_type, &sequence_state,
             &field_offset_in_cls](ReferenceTable::Cursor& ref) {
              while (current_type && field_offset_in_cls >=
                                         current_type->field_name_ids.size()) {
                size_t prev_type_size = current_type->field_name_ids.size();
                current_type = GetSuperClass(&sequence_state, current_type);
                field_offset_in_cls -= prev_type_size;
              }

              if (!current_type) {
                return false;
              }

              uint64_t field_id =
                  current_type->field_name_ids[field_offset_in_cls++];
              auto* ptr = sequence_state.interned_fields.Find(field_id);
              if (!ptr) {
                PERFETTO_DLOG("Invalid field id.");
                global_stats_tracker_->IncrementGlobalIndexedStats(
                    stats::heap_graph_malformed_packet,
                    static_cast<int>(sequence_state.current_upid));
                return true;
              }
              const InternedField& field = *ptr;
              ref.set_field_name(field.name);
              ref.set_field_type_name(field.type_name);
              field_to_rows_[field.name].emplace_back(ref.ToRowNumber());
              return true;
            });
      }
      sequence_state.deferred_reference_objects_for_type_.erase(ref_obj_it);
    }

    type_row_ref.set_name(interned_type.name);
    if (interned_type.classloader_id) {
      auto classloader_object_ref =
          GetOrInsertObject(&sequence_state, interned_type.classloader_id);
      type_row_ref.set_classloader_id(classloader_object_ref.id().value);
    }
    if (location_name) {
      type_row_ref.set_location(location_name);
    }
    type_row_ref.set_kind(InternTypeKindString(interned_type.kind));

    base::StringView normalized_type =
        NormalizeTypeName(storage_->GetString(interned_type.name));

    std::optional<StringId> class_package;
    if (location_name) {
      std::optional<std::string> package_name = PackageFromLocation(
          global_stats_tracker_, storage_->GetString(location_name));
      if (package_name) {
        class_package = storage_->InternString(base::StringView(*package_name));
      }
    }
    if (!class_package) {
      auto app_id = storage_->process_table()[sequence_state.current_upid]
                        .android_appid();
      if (app_id) {
        for (auto it = storage_->package_list_table().IterateRows(); it; ++it) {
          if (it.uid() == *app_id) {
            class_package = it.package_name();
            break;
          }
        }
      }
    }

    class_to_rows_[std::make_pair(class_package,
                                  storage_->InternString(normalized_type))]
        .emplace_back(type_row_ref.ToRowNumber());
  }

  if (!sequence_state.deferred_size_objects_for_type_.empty() ||
      !sequence_state.deferred_reference_objects_for_type_.empty()) {
    global_stats_tracker_->IncrementGlobalIndexedStats(
        stats::heap_graph_malformed_packet,
        static_cast<int>(sequence_state.current_upid));
  }

  SourceRoot internal_vm_roots;
  internal_vm_roots.root_type = ::com::android::art::tracing::pbzero::
      HeapGraphRoot::Type::ROOT_VM_INTERNAL;
  internal_vm_roots.object_ids = std::move(sequence_state.internal_vm_roots);
  sequence_state.internal_vm_roots.clear();
  sequence_state.current_roots.emplace_back(std::move(internal_vm_roots));

  for (const SourceRoot& root : sequence_state.current_roots) {
    for (uint64_t obj_id : root.object_ids) {
      auto* ptr = sequence_state.object_id_to_db_row.Find(obj_id);
      // This can only happen for an invalid type string id, which is already
      // reported as an error. Silently continue here.
      if (!ptr)
        continue;

      ObjectTable::RowReference row_ref =
          ptr->ToRowReference(storage_->mutable_heap_graph_object_table());
      MarkRoot(row_ref, InternRootTypeString(root.root_type));
    }
  }

  PopulateSuperClasses(sequence_state);
  PopulateNativeSize(sequence_state);

  auto& heap_graph_table = *storage_->mutable_heap_graph_table();
  std::optional<tables::HeapGraphTable::Id> heap_graph_id;
  heap_graph_cursor_.SetFilterValueUnchecked(0, sequence_state.current_upid);
  heap_graph_cursor_.SetFilterValueUnchecked(1, sequence_state.current_ts);
  heap_graph_cursor_.Execute();
  if (!heap_graph_cursor_.Eof()) {
    heap_graph_id = heap_graph_cursor_.id();
    heap_graph_cursor_.Next();
    PERFETTO_DCHECK(heap_graph_cursor_.Eof());
  }
  if (heap_graph_id) {
    if (sequence_state.heap_size) {
      auto row_ref = heap_graph_table[*heap_graph_id];
      row_ref.set_heap_size(*sequence_state.heap_size);
    }
    if (sequence_state.truncated) {
      auto row_ref = heap_graph_table[*heap_graph_id];
      row_ref.set_truncated(true);
    }
  } else {
    tables::HeapGraphTable::Row row;
    row.upid = sequence_state.current_upid;
    row.ts = sequence_state.current_ts;
    row.truncated = sequence_state.truncated;
    if (sequence_state.heap_size) {
      row.heap_size = *sequence_state.heap_size;
    }
    heap_graph_table.Insert(row);
  }

  sequence_state_.erase(seq_id);
}

std::optional<ObjectTable::Id> HeapGraphTracker::GetReferenceByFieldName(
    ObjectTable::Id obj,
    StringId field) {
  std::optional<ObjectTable::Id> referred;
  auto obj_row_ref = storage_->heap_graph_object_table()[obj];
  ForReferenceSet(reference_cursor_, obj_row_ref.reference_set_id(),
                  [&](ReferenceTable::Cursor& ref) -> bool {
                    if (ref.field_name() == field) {
                      referred = ref.owned_id();
                      return false;
                    }
                    return true;
                  });
  return referred;
}

void HeapGraphTracker::PopulateNativeSize(const SequenceState& seq) {
  //             +-------------------------------+  .referent   +--------+
  //             |       sun.misc.Cleaner        | -----------> | Object |
  //             +-------------------------------+              +--------+
  //                |
  //                | .thunk
  //                v
  // +----------------------------------------------------+
  // | libcore.util.NativeAllocationRegistry$CleanerThunk |
  // +----------------------------------------------------+
  //   |
  //   | .this$0
  //   v
  // +----------------------------------------------------+
  // |       libcore.util.NativeAllocationRegistry        |
  // |                       .size                        |
  // +----------------------------------------------------+
  //
  // `.size` should be attributed as the native size of Object

  auto& objects_tbl = *storage_->mutable_heap_graph_object_table();

  struct Cleaner {
    ObjectTable::Id referent;
    ObjectTable::Id thunk;
  };
  std::vector<Cleaner> cleaners;

  class_cursor_.SetFilterValueUnchecked(0, "sun.misc.Cleaner");
  for (class_cursor_.Execute(); !class_cursor_.Eof(); class_cursor_.Next()) {
    auto class_id = class_cursor_.id();
    object_cursor_.SetFilterValueUnchecked(0, class_id.value);
    object_cursor_.SetFilterValueUnchecked(1, seq.current_upid);
    object_cursor_.SetFilterValueUnchecked(2, seq.current_ts);
    for (object_cursor_.Execute(); !object_cursor_.Eof();
         object_cursor_.Next()) {
      ObjectTable::Id cleaner_obj_id = object_cursor_.id();
      std::optional<ObjectTable::Id> referent_id =
          GetReferenceByFieldName(cleaner_obj_id, referent_str_id_);
      std::optional<ObjectTable::Id> thunk_id =
          GetReferenceByFieldName(cleaner_obj_id, cleaner_thunk_str_id_);
      if (!referent_id || !thunk_id) {
        continue;
      }
      std::optional<ObjectTable::Id> next_id =
          GetReferenceByFieldName(cleaner_obj_id, cleaner_next_str_id_);
      if (next_id.has_value() && *next_id == cleaner_obj_id) {
        // sun.misc.Cleaner.next points to the sun.misc.Cleaner: this means
        // that the sun.misc.Cleaner.clean() has already been called. Skip this.
        continue;
      }
      cleaners.push_back(Cleaner{*referent_id, *thunk_id});
    }
  }

  for (const auto& cleaner : cleaners) {
    std::optional<ObjectTable::Id> this0 =
        GetReferenceByFieldName(cleaner.thunk, cleaner_thunk_this0_str_id_);
    if (!this0) {
      continue;
    }

    auto nar_size_it = seq.nar_size_by_obj_id.find(*this0);
    if (nar_size_it == seq.nar_size_by_obj_id.end()) {
      continue;
    }

    int64_t native_size =
        GetSizeFromNativeAllocationRegistry(nar_size_it->second);
    auto referent_row_ref = objects_tbl[cleaner.referent];
    int64_t total_native_size = referent_row_ref.native_size() + native_size;
    referent_row_ref.set_native_size(total_native_size);
  }
}

// TODO(fmayer): For Android S+ traces, use the superclass_id from the trace.
void HeapGraphTracker::PopulateSuperClasses(const SequenceState& seq) {
  // Maps from normalized class name and location, to superclass.
  std::map<ClassDescriptor, ClassDescriptor> superclass_map =
      BuildSuperclassMap(seq.current_upid, seq.current_ts, storage_,
                         superclass_cursor_, referred_cursor_);

  auto* classes_tbl = storage_->mutable_heap_graph_class_table();
  std::map<ClassDescriptor, ClassTable::Id> class_to_id;
  for (auto it = classes_tbl->IterateRows(); it; ++it) {
    class_to_id[{it.name(), it.location()}] = it.id();
  }

  // Iterate through the classes table and annotate with superclasses.
  // We iterate all rows on the classes table (even though the superclass
  // mapping was generated on the current sequence) - if we cannot identify
  // a superclass we will just skip.
  for (uint32_t i = 0; i < classes_tbl->row_count(); ++i) {
    auto rr = (*classes_tbl)[i];
    auto name = storage_->GetString(rr.name());
    auto location = rr.location();
    auto normalized = GetNormalizedType(name);
    if (normalized.is_static_class || normalized.number_of_arrays > 0)
      continue;

    StringId class_name_id = storage_->InternString(normalized.name);
    auto map_it = superclass_map.find({class_name_id, location});
    if (map_it == superclass_map.end()) {
      continue;
    }

    // Find the row for the superclass id
    auto superclass_it = class_to_id.find(map_it->second);
    if (superclass_it == class_to_id.end()) {
      // This can happen for traces was captured before the patch to
      // explicitly emit interned types (meaning classes without live
      // instances would not appear here).
      continue;
    }
    rr.set_superclass_id(superclass_it->second);
  }
}

void HeapGraphTracker::GetChildren(ObjectTable::RowReference object,
                                   std::vector<ObjectTable::Id>& children) {
  children.clear();

  auto cls_row_ref = storage_->heap_graph_class_table()[object.type_id()];

  StringId kind = cls_row_ref.kind();

  bool is_ignored_reference =
      kind == InternTypeKindString(::com::android::art::tracing::pbzero::
                                       HeapGraphType::KIND_WEAK_REFERENCE) ||
      kind ==
          InternTypeKindString(::com::android::art::tracing::pbzero::
                                   HeapGraphType::KIND_FINALIZER_REFERENCE) ||
      kind == InternTypeKindString(::com::android::art::tracing::pbzero::
                                       HeapGraphType::KIND_PHANTOM_REFERENCE);

  ForReferenceSet(
      reference_cursor_, object.reference_set_id(),
      [object, &children, is_ignored_reference,
       this](ReferenceTable::Cursor& ref) {
        PERFETTO_CHECK(ref.owner_id() == object.id());
        auto opt_owned = ref.owned_id();
        if (!opt_owned) {
          return true;
        }
        if (is_ignored_reference && ref.field_name() == referent_str_id_) {
          // If `object` is a special reference kind, its
          // "java.lang.ref.Reference.referent" field should be ignored.
          return true;
        }
        children.push_back(*opt_owned);
        return true;
      });
  std::sort(children.begin(), children.end(),
            [](const ObjectTable::Id& a, const ObjectTable::Id& b) {
              return a.value < b.value;
            });
  children.erase(std::unique(children.begin(), children.end()), children.end());
}

size_t HeapGraphTracker::RankRoot(StringId type) {
  size_t idx = 0;
  for (; idx < kRootTypePrecedence.size(); ++idx) {
    if (type == InternRootTypeString(kRootTypePrecedence[idx])) {
      break;
    }
  }
  return idx;
}

void HeapGraphTracker::MarkRoot(ObjectTable::RowReference row_ref,
                                StringId type) {
  // Already marked as a root
  if (row_ref.root_type()) {
    if (RankRoot(type) < RankRoot(*row_ref.root_type())) {
      row_ref.set_root_type(type);
    }
    return;
  }
  row_ref.set_root_type(type);

  std::vector<ObjectTable::Id> children;

  // DFS to mark reachability for all children
  std::vector<ObjectTable::RowReference> stack({row_ref});
  while (!stack.empty()) {
    ObjectTable::RowReference cur_node = stack.back();
    stack.pop_back();

    if (cur_node.reachable())
      continue;
    cur_node.set_reachable(true);

    GetChildren(cur_node, children);
    for (ObjectTable::Id child_node : children) {
      auto child_ref =
          (*storage_->mutable_heap_graph_object_table())[child_node];
      stack.push_back(child_ref);
    }
  }
}

void HeapGraphTracker::FinalizeAllProfiles() {
  if (!sequence_state_.empty()) {
    global_stats_tracker_->IncrementGlobalStats(
        stats::heap_graph_non_finalized_graph);
    // There might still be valuable data even though the trace is truncated.
    while (!sequence_state_.empty()) {
      FinalizeProfile(sequence_state_.begin()->first);
    }
  }

  // TODO(lalitm): when experimental_flamegraph is removed, we can remove all of
  // this.
  class_cursor_.Reset();
  object_cursor_.Reset();
  superclass_cursor_.Reset();
  reference_cursor_.Reset();
  referred_cursor_.Reset();
}

StringId HeapGraphTracker::InternRootTypeString(
    ::com::android::art::tracing::pbzero::HeapGraphRoot::Type root_type) {
  size_t idx = static_cast<size_t>(root_type);
  if (idx >= root_type_string_ids_.size()) {
    idx = static_cast<size_t>(
        ::com::android::art::tracing::pbzero::HeapGraphRoot::ROOT_UNKNOWN);
  }

  return root_type_string_ids_[idx];
}

StringId HeapGraphTracker::InternTypeKindString(
    ::com::android::art::tracing::pbzero::HeapGraphType::Kind kind) {
  size_t idx = static_cast<size_t>(kind);
  if (idx >= type_kind_string_ids_.size()) {
    idx = static_cast<size_t>(
        ::com::android::art::tracing::pbzero::HeapGraphType::KIND_UNKNOWN);
  }

  return type_kind_string_ids_[idx];
}

HeapGraphTracker::~HeapGraphTracker() = default;

}  // namespace perfetto::trace_processor
