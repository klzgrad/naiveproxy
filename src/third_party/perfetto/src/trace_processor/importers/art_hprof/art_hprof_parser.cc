/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/importers/art_hprof/art_hprof_parser.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/art_hprof/art_heap_graph.h"
#include "src/trace_processor/importers/art_hprof/art_heap_graph_builder.h"
#include "src/trace_processor/importers/art_hprof/art_hprof_model.h"
#include "src/trace_processor/importers/art_hprof/art_hprof_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::art_hprof {

ArtHprofParser::ArtHprofParser(TraceProcessorContext* context)
    : context_(context) {}

ArtHprofParser::~ArtHprofParser() = default;

base::Status ArtHprofParser::Parse(TraceBlobView blob) {
  bool is_init = false;
  if (!parser_) {
    byte_iterator_ = std::make_unique<TraceBlobViewIterator>();
    parser_ = std::make_unique<HeapGraphBuilder>(
        std::unique_ptr<ByteIterator>(byte_iterator_.release()), context_);
    is_init = true;
  }
  parser_->PushBlob(std::move(blob));

  if (is_init && !parser_->ParseHeader()) {
    context_->storage->IncrementStats(stats::hprof_header_errors);
  }

  parser_->Parse();

  return base::OkStatus();
}

base::Status ArtHprofParser::NotifyEndOfFile() {
  const HeapGraph graph = parser_->BuildGraph();

  UniquePid upid = context_->process_tracker->GetOrCreateProcess(0);

  if (graph.GetClassCount() == 0 || graph.GetObjectCount() == 0) {
    return base::OkStatus();
  }

  // Process classes first to establish type information
  PopulateClasses(graph);

  // Process objects next
  PopulateObjects(graph, static_cast<int64_t>(graph.GetTimestamp()), upid);

  // Finally process references
  PopulateReferences(graph);

  return base::OkStatus();
}

// Helper methods for map lookups
tables::HeapGraphClassTable::Id* ArtHprofParser::FindClassId(
    uint64_t class_id) const {
  return class_map_.Find(class_id);
}

tables::HeapGraphObjectTable::Id* ArtHprofParser::FindObjectId(
    uint64_t obj_id) const {
  return object_map_.Find(obj_id);
}

tables::HeapGraphClassTable::Id* ArtHprofParser::FindClassObjectId(
    uint64_t obj_id) const {
  return class_object_map_.Find(obj_id);
}

StringId ArtHprofParser::InternClassName(const std::string& class_name) {
  return context_->storage->InternString(class_name);
}

// TraceBlobViewIterator implementation
ArtHprofParser::TraceBlobViewIterator::TraceBlobViewIterator()
    : current_offset_(0) {}

ArtHprofParser::TraceBlobViewIterator::~TraceBlobViewIterator() = default;

bool ArtHprofParser::TraceBlobViewIterator::ReadU1(uint8_t& value) {
  auto slice = reader_.SliceOff(current_offset_, 1);
  if (!slice)
    return false;
  value = *slice->data();
  current_offset_ += 1;
  return true;
}

bool ArtHprofParser::TraceBlobViewIterator::ReadU2(uint16_t& value) {
  uint8_t b1;
  uint8_t b2;
  if (!ReadU1(b1) || !ReadU1(b2))
    return false;
  value = static_cast<uint16_t>((static_cast<uint16_t>(b1) << 8) |
                                static_cast<uint16_t>(b2));
  return true;
}

bool ArtHprofParser::TraceBlobViewIterator::ReadU4(uint32_t& value) {
  uint8_t b1;
  uint8_t b2;
  uint8_t b3;
  uint8_t b4;
  if (!ReadU1(b1) || !ReadU1(b2) || !ReadU1(b3) || !ReadU1(b4))
    return false;
  value = (static_cast<uint32_t>(b1) << 24) |
          (static_cast<uint32_t>(b2) << 16) | (static_cast<uint32_t>(b3) << 8) |
          static_cast<uint32_t>(b4);
  return true;
}

bool ArtHprofParser::TraceBlobViewIterator::ReadId(uint64_t& value,
                                                   uint32_t id_size) {
  if (id_size == 4) {
    uint32_t id;
    if (!ReadU4(id))
      return false;
    value = id;
    return true;
  }
  if (id_size == 8) {
    uint32_t high;
    uint32_t low;
    if (!ReadU4(high) || !ReadU4(low))
      return false;
    value = (static_cast<uint64_t>(high) << 32) | low;
    return true;
  }
  return false;
}

bool ArtHprofParser::TraceBlobViewIterator::ReadString(std::string& str,
                                                       size_t length) {
  auto slice = reader_.SliceOff(current_offset_, length);
  if (!slice)
    return false;

  str.resize(length);
  std::memcpy(str.data(), slice->data(), length);
  current_offset_ += length;
  return true;
}

bool ArtHprofParser::TraceBlobViewIterator::ReadBytes(
    std::vector<uint8_t>& data,
    size_t length) {
  auto slice = reader_.SliceOff(current_offset_, length);
  if (!slice)
    return false;

  data.resize(length);
  std::memcpy(data.data(), slice->data(), length);
  current_offset_ += length;
  return true;
}

bool ArtHprofParser::TraceBlobViewIterator::SkipBytes(size_t count) {
  auto slice = reader_.SliceOff(current_offset_, count);
  if (!slice)
    return false;

  current_offset_ += count;
  return true;
}

size_t ArtHprofParser::TraceBlobViewIterator::GetPosition() const {
  return current_offset_;
}

bool ArtHprofParser::TraceBlobViewIterator::CanReadRecord() const {
  const size_t base_offset = current_offset_ + kRecordLengthOffset;
  uint8_t bytes[4];

  auto slice = reader_.SliceOff(base_offset, 4);
  if (!slice) {
    return false;
  }

  memcpy(bytes, slice->data(), 4);

  uint64_t record_length = (static_cast<uint32_t>(bytes[0]) << 24) |
                           (static_cast<uint32_t>(bytes[1]) << 16) |
                           (static_cast<uint32_t>(bytes[2]) << 8) |
                           static_cast<uint32_t>(bytes[3]);

  // Check if we can read an entire record from the chunk.
  // If we can't we should fail so that we can receive another
  // chunk to continue.
  return static_cast<bool>(reader_.SliceOff(current_offset_, record_length));
}

void ArtHprofParser::TraceBlobViewIterator::PushBlob(TraceBlobView blob) {
  reader_.PushBack(std::move(blob));
}

void ArtHprofParser::TraceBlobViewIterator::Shrink() {
  reader_.PopFrontUntil(current_offset_);
}

void ArtHprofParser::PopulateClasses(const HeapGraph& graph) {
  auto& class_table = *context_->storage->mutable_heap_graph_class_table();

  // Process each class from the heap graph
  for (auto it = graph.GetClasses().GetIterator(); it; ++it) {
    auto class_id = it.key();
    auto& class_def = it.value();

    // Intern strings for class metadata
    StringId name_id = InternClassName(class_def.GetName());
    StringId kind_id = context_->storage->InternString(kUnknownClassKind);

    // Create and insert the class row
    tables::HeapGraphClassTable::Row class_row;
    class_row.name = name_id;
    class_row.deobfuscated_name = std::nullopt;
    class_row.location = std::nullopt;
    class_row.superclass_id = std::nullopt;  // Will update in second pass
    class_row.classloader_id = 0;            // Default
    class_row.kind = kind_id;

    tables::HeapGraphClassTable::Id table_id = class_table.Insert(class_row).id;
    class_map_[class_id] = table_id;
    class_name_map_[class_id] = class_def.GetName();
  }

  // Update superclass relationships
  for (auto it = graph.GetClasses().GetIterator(); it; ++it) {
    auto class_id = it.key();
    auto& class_def = it.value();
    uint64_t super_id = class_def.GetSuperClassId();
    if (super_id != 0) {
      auto* current_id = FindClassId(class_id);
      auto* super_id_opt = FindClassId(super_id);

      if (current_id && super_id_opt) {
        auto ref = class_table.FindById(*current_id);
        ref->set_superclass_id(*super_id_opt);
      }
    }
  }

  // Process class objects
  for (auto it = graph.GetObjects().GetIterator(); it; ++it) {
    auto obj_id = it.key();
    auto& obj = it.value();

    if (obj.GetObjectType() == ObjectType::kClass) {
      auto* class_name_it = class_name_map_.Find(obj.GetClassId());
      if (!class_name_it) {
        context_->storage->IncrementStats(stats::hprof_class_errors);
        continue;
      }

      // Intern strings for class metadata
      StringId name_id =
          InternClassName("java.lang.Class<" + *class_name_it + ">");
      StringId kind_id = context_->storage->InternString(kUnknownClassKind);

      // Create and insert the class row
      tables::HeapGraphClassTable::Row class_row;
      class_row.name = name_id;
      class_row.deobfuscated_name = std::nullopt;
      class_row.location = std::nullopt;
      class_row.superclass_id = std::nullopt;
      class_row.classloader_id = 0;
      class_row.kind = kind_id;

      tables::HeapGraphClassTable::Id table_id =
          class_table.Insert(class_row).id;
      class_object_map_[obj_id] = table_id;
    }
  }
}

void ArtHprofParser::PopulateObjects(const HeapGraph& graph,
                                     int64_t ts,
                                     UniquePid upid) {
  auto& object_table = *context_->storage->mutable_heap_graph_object_table();

  // Create fallback unknown class if needed
  tables::HeapGraphClassTable::Id unknown_class_id;

  for (auto it = graph.GetObjects().GetIterator(); it; ++it) {
    auto obj_id = it.key();
    auto& obj = it.value();

    tables::HeapGraphClassTable::Id* type_id;

    if (obj.GetObjectType() == ObjectType::kClass) {
      type_id = FindClassObjectId(obj.GetId());
      if (!type_id) {
        context_->storage->IncrementStats(stats::hprof_class_errors);
        continue;
      }
    } else {
      // Resolve object's type
      type_id = FindClassId(obj.GetClassId());
      if (!type_id && obj.GetObjectType() != ObjectType::kPrimitiveArray) {
        context_->storage->IncrementStats(stats::hprof_class_errors);
        continue;
      }
    }

    // Create object row
    tables::HeapGraphObjectTable::Row object_row;
    object_row.upid = upid;
    object_row.graph_sample_ts = ts;
    object_row.self_size = static_cast<int64_t>(obj.GetSize());
    object_row.native_size = obj.GetNativeSize();
    object_row.reference_set_id = std::nullopt;
    object_row.reachable = obj.IsReachable();
    object_row.type_id = type_id ? *type_id : unknown_class_id;

    // Handle heap type
    StringId heap_type_id = context_->storage->InternString(obj.GetHeapType());
    object_row.heap_type = heap_type_id;

    // Handle root type
    if (obj.IsRoot() && obj.GetRootType().has_value()) {
      // Convert root type enum to string
      std::string root_type_str =
          HeapGraph::GetRootTypeName(obj.GetRootType().value());
      StringId root_type_id = context_->storage->InternString(
          base::StringView(root_type_str.data(), root_type_str.size()));
      object_row.root_type = root_type_id;
    }

    object_row.root_distance = -1;  // Ignored

    // Insert object and store mapping
    tables::HeapGraphObjectTable::Id table_id =
        object_table.Insert(object_row).id;
    object_map_[obj_id] = table_id;
  }
}

void ArtHprofParser::PopulateReferences(const HeapGraph& graph) {
  auto& object_table = *context_->storage->mutable_heap_graph_object_table();
  auto& reference_table =
      *context_->storage->mutable_heap_graph_reference_table();
  auto& class_table = *context_->storage->mutable_heap_graph_class_table();

  // Group references by owner for efficient reference_set_id assignment
  base::FlatHashMap<uint64_t, std::vector<Reference>> refs_by_owner;

  // Step 1: Collect all references
  for (auto it = graph.GetObjects().GetIterator(); it; ++it) {
    auto obj_id = it.key();
    auto& obj = it.value();

    const auto& refs = obj.GetReferences();
    if (!refs.empty()) {
      refs_by_owner[obj_id].insert(refs_by_owner[obj_id].end(), refs.begin(),
                                   refs.end());
    }
  }

  // Step 2: Validate we have reference owners in our object map
  size_t missing_owners = 0;
  for (auto it = refs_by_owner.GetIterator(); it; ++it) {
    auto owner_id = it.key();
    if (!FindObjectId(owner_id)) {
      missing_owners++;
    }
  }

  if (missing_owners > 0) {
    context_->storage->IncrementStats(stats::hprof_reference_errors);
  }

  // Step 3: Build class map for type resolution
  base::FlatHashMap<uint64_t, tables::HeapGraphClassTable::Id> field_class_map;
  for (auto it = graph.GetClasses().GetIterator(); it; ++it) {
    auto class_id = it.key();
    auto& class_def = it.value();
    StringId name_id = InternClassName(class_def.GetName());

    // Find the class ID in the table
    for (uint32_t i = 0; i < class_table.row_count(); i++) {
      if (class_table[i].name() == name_id) {
        field_class_map[class_id] = tables::HeapGraphClassTable::Id(i);
        break;
      }
    }
  }

  // Step 4: Process references and create reference sets
  uint32_t next_reference_set_id = 1;

  for (auto it = refs_by_owner.GetIterator(); it; ++it) {
    auto owner_id = it.key();
    auto& refs = it.value();
    // Skip if no references
    if (refs.empty()) {
      continue;
    }

    // Get owner's table ID
    auto* owner_id_opt = FindObjectId(owner_id);
    if (!owner_id_opt) {
      continue;
    }

    // Create reference set for owner
    uint32_t reference_set_id = next_reference_set_id++;
    object_table.FindById(*owner_id_opt)
        ->set_reference_set_id(reference_set_id);

    // Process all references from this owner
    for (const auto& ref : refs) {
      // Get owned object's table ID if it exists
      tables::HeapGraphObjectTable::Id* owned_table_id = nullptr;
      if (ref.target_id != 0) {
        owned_table_id = FindObjectId(ref.target_id);
        if (!owned_table_id) {
          context_->storage->IncrementStats(stats::hprof_reference_errors);
        }
      }

      // Get the field name
      StringId field_name_id = context_->storage->InternString(ref.field_name);

      // Resolve field type from class ID
      StringId field_type_id;
      auto* cls = field_class_map.Find(*ref.field_class_id);
      if (cls) {
        // Get class name from class table
        field_type_id = class_table[cls->value].name();
      } else {
        context_->storage->IncrementStats(stats::hprof_class_errors);
        continue;
      }

      // Create reference record
      tables::HeapGraphReferenceTable::Row reference_row;
      reference_row.reference_set_id = reference_set_id;
      reference_row.owner_id = *owner_id_opt;
      reference_row.owned_id =
          owned_table_id
              ? std::optional<tables::HeapGraphObjectTable::Id>(*owned_table_id)
              : std::nullopt;
      reference_row.field_name = field_name_id;
      reference_row.field_type_name = field_type_id;

      reference_table.Insert(reference_row);
    }
  }
}
}  // namespace perfetto::trace_processor::art_hprof
