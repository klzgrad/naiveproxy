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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GLOBAL_METADATA_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GLOBAL_METADATA_TRACKER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

// Tracks information in the metadata table globally across all machines and
// traces.
class GlobalMetadataTracker {
 private:
  using MachineId = tables::MachineTable::Id;
  using TraceId = tables::TraceFileTable::Id;

  struct MetadataEntry {
    StringId name;
    std::optional<MachineId> machine_id;
    std::optional<TraceId> trace_id;

    bool operator==(const MetadataEntry& other) const {
      return name == other.name && machine_id == other.machine_id &&
             trace_id == other.trace_id;
    }

    template <typename H>
    friend H PerfettoHashValue(H h, const MetadataEntry& value) {
      return H::Combine(std::move(h), value.name.raw_id(), value.machine_id,
                        value.trace_id);
    }
  };

 public:
  explicit GlobalMetadataTracker(TraceStorage* storage);

  // Sets a metadata entry. If an entry with the same name, machine_id, and
  // trace_id already exists, it is updated.
  // Returns the id of the entry.
  MetadataId SetMetadata(std::optional<MachineId> machine_id,
                         std::optional<TraceId> trace_id,
                         metadata::KeyId key,
                         Variadic value);

  // Appends a metadata entry. Multiple entries with the same name, machine_id,
  // and trace_id can exist.
  // Returns the id of the new entry.
  MetadataId AppendMetadata(std::optional<MachineId> machine_id,
                            std::optional<TraceId> trace_id,
                            metadata::KeyId key,
                            Variadic value);

  // Sets a metadata entry using any interned string as key.
  // Returns the id of the new entry.
  MetadataId SetDynamicMetadata(std::optional<MachineId> machine_id,
                                std::optional<TraceId> trace_id,
                                StringId key,
                                Variadic value);

  // Reads back a set metadata value.
  // Only kSingle types are supported right now.
  std::optional<SqlValue> GetMetadata(std::optional<MachineId> machine_id,
                                      std::optional<TraceId> trace_id,
                                      metadata::KeyId key) const;

 private:
  static constexpr size_t kNumKeys =
      static_cast<size_t>(metadata::KeyId::kNumKeys);
  static constexpr size_t kNumKeyTypes =
      static_cast<size_t>(metadata::KeyType::kNumKeyTypes);

  struct ContextIds {
    std::optional<MachineId> machine_id;
    std::optional<TraceId> trace_id;
  };

  void WriteValue(tables::MetadataTable::RowReference rr, Variadic value);
  ContextIds GetContextIds(metadata::KeyId key,
                           std::optional<MachineId> machine_id,
                           std::optional<TraceId> trace_id) const;

  std::array<StringId, kNumKeys> key_ids_;
  std::array<StringId, kNumKeyTypes> key_type_ids_;

  base::FlatHashMap<MetadataEntry, MetadataId> id_by_entry_;
  TraceStorage* const storage_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GLOBAL_METADATA_TRACKER_H_
