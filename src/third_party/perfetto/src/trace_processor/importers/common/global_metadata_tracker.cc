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

#include "src/trace_processor/importers/common/global_metadata_tracker.h"

#include <cstddef>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/crash_keys.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

namespace {
base::CrashKey g_crash_key_uuid("trace_uuid");
}

GlobalMetadataTracker::GlobalMetadataTracker(TraceStorage* storage)
    : storage_(storage) {
  for (size_t i = 0; i < kNumKeys; ++i) {
    key_ids_[i] = storage->InternString(metadata::kNames[i]);
  }
  for (size_t i = 0; i < kNumKeyTypes; ++i) {
    key_type_ids_[i] = storage->InternString(metadata::kKeyTypeNames[i]);
  }
}

MetadataId GlobalMetadataTracker::SetMetadata(
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id,
    metadata::KeyId key,
    Variadic value) {
  PERFETTO_CHECK(metadata::kKeyTypes[key] == metadata::KeyType::kSingle);
  PERFETTO_CHECK(value.type == metadata::kValueTypes[key]);

  // When the trace_uuid is set, store a copy in a crash key, so in case of
  // a crash in the pipelines we can tell which trace caused the crash.
  if (key == metadata::trace_uuid && value.type == Variadic::kString) {
    g_crash_key_uuid.Set(storage_->GetString(value.string_value));
  }

  auto ctx_ids = GetContextIds(key, machine_id, trace_id);
  auto& metadata_table = *storage_->mutable_metadata_table();
  StringId name_id = key_ids_[static_cast<size_t>(key)];

  MetadataEntry entry{name_id, ctx_ids.machine_id, ctx_ids.trace_id};
  if (auto* id_ptr = id_by_entry_.Find(entry)) {
    WriteValue(*metadata_table.FindById(*id_ptr), value);
    return *id_ptr;
  }

  // Special case for trace_uuid: it's possible that trace_uuid was set
  // globally (with trace_id=null) before the actual trace_id was known (e.g. by
  // TraceProcessorStorageImpl). In this case, we "upgrade" the existing global
  // entry by associating it with the current trace context instead of
  // inserting a new row.
  if (key == metadata::trace_uuid) {
    MetadataEntry global_entry{name_id, std::nullopt, std::nullopt};
    if (auto* id_ptr = id_by_entry_.Find(global_entry)) {
      MetadataId id = *id_ptr;
      id_by_entry_.Erase(global_entry);
      id_by_entry_.Insert(entry, id);

      auto rr = *metadata_table.FindById(id);
      rr.set_trace_id(ctx_ids.trace_id);
      WriteValue(rr, value);
      return id;
    }
  }

  tables::MetadataTable::Row row;
  row.name = name_id;
  row.key_type = key_type_ids_[static_cast<size_t>(metadata::KeyType::kSingle)];
  row.machine_id = ctx_ids.machine_id;
  row.trace_id = ctx_ids.trace_id;

  auto id_and_row = metadata_table.Insert(row);
  WriteValue(metadata_table[id_and_row.row], value);
  id_by_entry_.Insert(entry, id_and_row.id);
  return id_and_row.id;
}

std::optional<SqlValue> GlobalMetadataTracker::GetMetadata(
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id,
    metadata::KeyId key) const {
  // KeyType::kMulti not yet supported by this method.
  PERFETTO_CHECK(metadata::kKeyTypes[key] == metadata::KeyType::kSingle);

  auto ctx_ids = GetContextIds(key, machine_id, trace_id);
  const auto& metadata_table = storage_->metadata_table();
  StringId name_id = key_ids_[static_cast<size_t>(key)];

  MetadataEntry entry{name_id, ctx_ids.machine_id, ctx_ids.trace_id};
  if (auto* id_ptr = id_by_entry_.Find(entry)) {
    auto rr = *metadata_table.FindById(*id_ptr);
    auto value_type = metadata::kValueTypes[key];
    switch (value_type) {
      case Variadic::kInt:
        return SqlValue::Long(*rr.int_value());
      case Variadic::kString:
        return SqlValue::String(storage_->GetString(*rr.str_value()).c_str());
      case Variadic::kNull:
        return SqlValue();
      case Variadic::kUint:
      case Variadic::kReal:
      case Variadic::kPointer:
      case Variadic::kBool:
      case Variadic::kJson:
        PERFETTO_FATAL("Invalid metadata value type %s",
                       Variadic::kTypeNames[value_type]);
    }
  }
  return std::nullopt;
}

MetadataId GlobalMetadataTracker::AppendMetadata(
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id,
    metadata::KeyId key,
    Variadic value) {
  PERFETTO_CHECK(key < metadata::kNumKeys);
  PERFETTO_CHECK(metadata::kKeyTypes[key] == metadata::KeyType::kMulti);
  PERFETTO_CHECK(value.type == metadata::kValueTypes[key]);

  auto ctx_ids = GetContextIds(key, machine_id, trace_id);
  auto& metadata_table = *storage_->mutable_metadata_table();

  tables::MetadataTable::Row row;
  row.name = key_ids_[static_cast<size_t>(key)];
  row.key_type = key_type_ids_[static_cast<size_t>(metadata::KeyType::kMulti)];
  row.machine_id = ctx_ids.machine_id;
  row.trace_id = ctx_ids.trace_id;

  auto id_and_row = metadata_table.Insert(row);
  WriteValue(metadata_table[id_and_row.row], value);
  return id_and_row.id;
}

MetadataId GlobalMetadataTracker::SetDynamicMetadata(
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id,
    StringId key,
    Variadic value) {
  auto& metadata_table = *storage_->mutable_metadata_table();
  tables::MetadataTable::Row row;
  row.name = key;
  row.key_type = key_type_ids_[static_cast<size_t>(metadata::KeyType::kSingle)];
  row.machine_id = machine_id;
  row.trace_id = trace_id;

  auto id_and_row = metadata_table.Insert(row);
  WriteValue(metadata_table[id_and_row.row], value);
  return id_and_row.id;
}

void GlobalMetadataTracker::WriteValue(tables::MetadataTable::RowReference rr,
                                       Variadic value) {
  switch (value.type) {
    case Variadic::Type::kInt:
      rr.set_int_value(value.int_value);
      break;
    case Variadic::Type::kString:
      rr.set_str_value(value.string_value);
      break;
    case Variadic::Type::kJson:
      rr.set_str_value(value.json_value);
      break;
    case Variadic::Type::kBool:
    case Variadic::Type::kPointer:
    case Variadic::Type::kUint:
    case Variadic::Type::kReal:
    case Variadic::Type::kNull:
      PERFETTO_FATAL("Unsupported value type %s",
                     Variadic::kTypeNames[value.type]);
  }
}

GlobalMetadataTracker::ContextIds GlobalMetadataTracker::GetContextIds(
    metadata::KeyId key,
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id) const {
  // Exception for trace_uuid: it can be called with null trace_id initially
  // from TraceProcessorStorageImpl when parsing hasn just started.
  if (key == metadata::trace_uuid) {
    return {std::nullopt, trace_id};
  }

  switch (metadata::kScopes[key]) {
    case metadata::Scope::kGlobal:
      return {std::nullopt, std::nullopt};
    case metadata::Scope::kMachine:
      PERFETTO_CHECK(machine_id.has_value());
      return {machine_id, std::nullopt};
    case metadata::Scope::kTrace:
      PERFETTO_CHECK(trace_id.has_value());
      return {std::nullopt, trace_id};
    case metadata::Scope::kMachineAndTrace:
      PERFETTO_CHECK(machine_id.has_value());
      PERFETTO_CHECK(trace_id.has_value());
      return {machine_id, trace_id};
    case metadata::Scope::kNumScopes:
      PERFETTO_FATAL("Invalid scope");
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace perfetto::trace_processor
