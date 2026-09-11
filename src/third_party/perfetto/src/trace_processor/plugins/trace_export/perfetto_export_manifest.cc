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

#include "src/trace_processor/plugins/trace_export/perfetto_export_manifest.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/util/simple_json_serializer.h"

namespace perfetto::trace_processor::trace_export {
namespace {

using core::dataframe::ColumnSpec;
using core::dataframe::DataframeSpec;

const char* StorageTypeToString(core::StorageType type) {
  if (type.Is<core::Id>()) {
    return "id";
  }
  if (type.Is<core::Uint32>()) {
    return "uint32";
  }
  if (type.Is<core::Int32>()) {
    return "int32";
  }
  if (type.Is<core::Int64>()) {
    return "int64";
  }
  if (type.Is<core::Double>()) {
    return "double";
  }
  PERFETTO_CHECK(type.Is<core::String>());
  return "string";
}

const char* NullabilityToString(core::Nullability n) {
  if (n.Is<core::NonNull>()) {
    return "non_null";
  }
  if (n.Is<core::DenseNull>()) {
    return "dense_null";
  }
  if (n.Is<core::SparseNull>()) {
    return "sparse_null";
  }
  if (n.Is<core::SparseNullWithPopcountAlways>()) {
    return "sparse_null_popcount_always";
  }
  PERFETTO_CHECK(n.Is<core::SparseNullWithPopcountUntilFinalization>());
  return "sparse_null_popcount_until_finalization";
}

const char* SortStateToString(core::SortState s) {
  if (s.Is<core::IdSorted>()) {
    return "id_sorted";
  }
  if (s.Is<core::SetIdSorted>()) {
    return "set_id_sorted";
  }
  if (s.Is<core::Sorted>()) {
    return "sorted";
  }
  PERFETTO_CHECK(s.Is<core::Unsorted>());
  return "unsorted";
}

const char* DuplicateStateToString(core::DuplicateState d) {
  if (d.Is<core::NoDuplicates>()) {
    return "no_duplicates";
  }
  PERFETTO_CHECK(d.Is<core::HasDuplicates>());
  return "has_duplicates";
}

// The manifest records the full nullability string; for compatibility only
// the nullable-vs-non-null distinction matters (it determines the Arrow wire
// format), so storage-strategy changes between builds stay compatible.
bool IsNullableString(std::string_view s) {
  return s != "non_null";
}

base::Status ValidateTableSchema(
    const TraceManifestState::PerfettoExportTable& table,
    const core::dataframe::Dataframe& df) {
  const char* name = table.name.c_str();
  DataframeSpec spec = df.CreateSpec();
  if (table.columns.size() != spec.column_names.size()) {
    return base::ErrStatus(
        "perfetto_export: schema mismatch for table '%s': archive has %zu "
        "columns but this version of trace processor has %zu",
        name, table.columns.size(), spec.column_names.size());
  }
  for (uint32_t i = 0; i < spec.column_names.size(); i++) {
    const TraceManifestState::PerfettoExportTableColumn& col = table.columns[i];
    if (col.name != spec.column_names[i]) {
      return base::ErrStatus(
          "perfetto_export: schema mismatch for table '%s': column %u is '%s' "
          "in the archive but '%s' in this version of trace processor",
          name, i, col.name.c_str(), spec.column_names[i].c_str());
    }
    const ColumnSpec& cs = spec.column_specs[i];
    if (col.type != StorageTypeToString(cs.type)) {
      return base::ErrStatus(
          "perfetto_export: schema mismatch for table '%s', column '%s': "
          "type is '%s' in the archive but '%s' in this version of trace "
          "processor",
          name, col.name.c_str(), col.type.c_str(),
          StorageTypeToString(cs.type));
    }
    if (IsNullableString(col.nullability) !=
        IsNullableString(NullabilityToString(cs.nullability))) {
      return base::ErrStatus(
          "perfetto_export: schema mismatch for table '%s', column '%s': "
          "nullability is '%s' in the archive but '%s' in this version of "
          "trace processor",
          name, col.name.c_str(), col.nullability.c_str(),
          NullabilityToString(cs.nullability));
    }
    if (col.sort != SortStateToString(cs.sort_state)) {
      return base::ErrStatus(
          "perfetto_export: schema mismatch for table '%s', column '%s': "
          "sort state is '%s' in the archive but '%s' in this version of "
          "trace processor",
          name, col.name.c_str(), col.sort.c_str(),
          SortStateToString(cs.sort_state));
    }
    if (col.duplicates != DuplicateStateToString(cs.duplicate_state)) {
      return base::ErrStatus(
          "perfetto_export: schema mismatch for table '%s', column '%s': "
          "duplicate state is '%s' in the archive but '%s' in this version "
          "of trace processor",
          name, col.name.c_str(), col.duplicates.c_str(),
          DuplicateStateToString(cs.duplicate_state));
    }
  }
  return base::OkStatus();
}

void WriteColumns(json::JsonArraySerializer& columns,
                  const DataframeSpec& spec) {
  for (uint32_t i = 0; i < spec.column_names.size(); i++) {
    columns.AppendDict([&](json::JsonDictSerializer& column) {
      const ColumnSpec& column_spec = spec.column_specs[i];
      column.AddString("name", spec.column_names[i]);
      column.AddString("type", StorageTypeToString(column_spec.type));
      column.AddString("nullability",
                       NullabilityToString(column_spec.nullability));
      column.AddString("sort", SortStateToString(column_spec.sort_state));
      column.AddString("duplicates",
                       DuplicateStateToString(column_spec.duplicate_state));
    });
  }
}

void WriteFile(json::JsonDictSerializer& file, const PluginDataframe& table) {
  DataframeSpec spec = table.dataframe->CreateSpec();
  file.AddString("path", table.name + ".arrow");
  file.AddDict(
      "__exported_table_schema", [&](json::JsonDictSerializer& schema) {
        schema.AddInt("format", kPerfettoExportFormatVersion);
        schema.AddString("name", table.name);
        schema.AddUint("row_count", table.dataframe->row_count());
        schema.AddArray("columns", [&](json::JsonArraySerializer& columns) {
          WriteColumns(columns, spec);
        });
      });
}

void WriteFiles(json::JsonArraySerializer& files,
                const std::vector<const PluginDataframe*>& tables) {
  for (const PluginDataframe* table : tables) {
    files.AppendDict(
        [&](json::JsonDictSerializer& file) { WriteFile(file, *table); });
  }
}

void WriteManifest(json::JsonDictSerializer& manifest,
                   const std::vector<const PluginDataframe*>& tables,
                   const char* writer_version) {
  manifest.AddInt("version", 1);
  manifest.AddDict("attributes", [&](json::JsonDictSerializer& attributes) {
    attributes.AddString("perfetto_export_writer_version", writer_version);
  });
  manifest.AddArray("files", [&](json::JsonArraySerializer& files) {
    WriteFiles(files, tables);
  });
}

}  // namespace

std::string SerializePerfettoExportManifest(
    const std::vector<const PluginDataframe*>& tables,
    const char* writer_version) {
  return json::SerializeJson([&](json::JsonValueSerializer&& root) {
    std::move(root).WriteDict([&](json::JsonDictSerializer& top) {
      top.AddDict("perfetto_manifest", [&](json::JsonDictSerializer& manifest) {
        WriteManifest(manifest, tables, writer_version);
      });
    });
  });
}

base::StatusOr<std::vector<ResolvedPerfettoExportTable>>
ValidatePerfettoExportTables(
    const std::vector<TraceManifestState::FileEntry>& files,
    const std::vector<PluginDataframe>& live) {
  std::vector<ResolvedPerfettoExportTable> result;
  for (const TraceManifestState::FileEntry& entry : files) {
    if (!entry.exported_table_schema) {
      continue;
    }
    const TraceManifestState::PerfettoExportTable& table =
        *entry.exported_table_schema;
    if (table.format != kPerfettoExportFormatVersion) {
      return base::ErrStatus(
          "perfetto_export: unsupported format version %" PRId64
          " for table '%s' (this version of trace processor supports version "
          "%" PRId64
          "). The archive was written by an incompatible version of trace "
          "processor; re-export it from the original trace.",
          table.format, table.name.c_str(), kPerfettoExportFormatVersion);
    }
    if (std::any_of(result.begin(), result.end(),
                    [&](const ResolvedPerfettoExportTable& existing) {
                      return existing.entry->exported_table_schema->name ==
                             table.name;
                    })) {
      return base::ErrStatus("perfetto_export: duplicate table '%s'",
                             table.name.c_str());
    }
    auto dataframe = std::find_if(
        live.begin(), live.end(),
        [&](const PluginDataframe& df) { return df.name == table.name; });
    if (dataframe == live.end()) {
      return base::ErrStatus(
          "perfetto_export: archive contains table '%s' which does not exist "
          "in this version of trace processor",
          table.name.c_str());
    }
    if (dataframe->dataframe->row_count() != 0) {
      return base::ErrStatus(
          "perfetto_export: table '%s' is not empty; an export can only be "
          "loaded into a fresh trace processor instance",
          table.name.c_str());
    }
    RETURN_IF_ERROR(ValidateTableSchema(table, *dataframe->dataframe));
    result.push_back({&entry, dataframe->dataframe});
  }
  return std::move(result);
}

}  // namespace perfetto::trace_processor::trace_export
