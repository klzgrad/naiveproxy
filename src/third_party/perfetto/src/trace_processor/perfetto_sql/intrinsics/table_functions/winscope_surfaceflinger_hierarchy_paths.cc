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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/winscope_surfaceflinger_hierarchy_paths.h"
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/basic_types.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/dataframe/typed_cursor.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_extractor.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"
#include "src/trace_processor/tables/winscope_tables_py.h"

namespace perfetto::trace_processor {

namespace {
using LayerDecoder = protos::pbzero::LayerProto::Decoder;

std::vector<int32_t> GetHierarchyPath(
    const LayerDecoder& layer_decoder,
    const std::unordered_map<int32_t, LayerDecoder>& layers_by_id) {
  std::vector<int32_t> hierarchy_path = {layer_decoder.id()};
  auto pos = layers_by_id.find(layer_decoder.parent());
  while (pos != layers_by_id.end()) {
    const auto& layer_dec = pos->second;
    auto id = layer_dec.id();
    auto parent = layer_dec.parent();
    if (id == parent) {
      // handles recursive hierarchies
      break;
    }
    hierarchy_path.push_back(id);
    pos = layers_by_id.find(parent);
  }
  return hierarchy_path;
}

base::Status InsertRows(
    const dataframe::Dataframe& snapshot_table,
    tables::WinscopeSurfaceFlingerHierarchyPathTable* paths_table,
    StringPool* string_pool) {
  constexpr auto kSpec = tables::SurfaceFlingerLayersSnapshotTable::kSpec;

  for (uint32_t i = 0; i < snapshot_table.row_count(); ++i) {
    auto base64_proto_id =
        snapshot_table
            .GetCellUnchecked<tables::SurfaceFlingerLayersSnapshotTable::
                                  ColumnIndex::base64_proto_id>(kSpec, i);
    PERFETTO_CHECK(base64_proto_id.has_value());

    const auto raw_proto =
        string_pool->Get(StringPool::Id::Raw(*base64_proto_id));
    const auto blob = *base::Base64Decode(raw_proto);
    const auto cb = protozero::ConstBytes{
        reinterpret_cast<const uint8_t*>(blob.data()), blob.size()};
    protos::pbzero::LayersSnapshotProto::Decoder snapshot(cb);
    protos::pbzero::LayersProto::Decoder layers(snapshot.layers());

    const auto& layers_by_id =
        winscope::surfaceflinger_layers::ExtractLayersById(layers);

    for (auto it = layers.layers(); it; ++it) {
      LayerDecoder layer(*it);
      if (!layer.has_id()) {
        continue;
      }
      auto layer_id = static_cast<uint32_t>(layer.id());

      auto path = GetHierarchyPath(layer, layers_by_id);
      for (auto path_it = path.rbegin(); path_it != path.rend(); ++path_it) {
        tables::WinscopeSurfaceFlingerHierarchyPathTable::Row row;
        row.snapshot_id = i;
        row.layer_id = layer_id;
        row.ancestor_id = static_cast<uint32_t>(*path_it);
        paths_table->Insert(row);
      }
    }
  }
  return base::OkStatus();
}
}  // namespace

WinscopeSurfaceFlingerHierarchyPaths::Cursor::Cursor(
    StringPool* string_pool,
    const PerfettoSqlEngine* engine)
    : string_pool_(string_pool), engine_(engine), table_(string_pool) {}

bool WinscopeSurfaceFlingerHierarchyPaths::Cursor::Run(
    const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 0);
  auto table_name = tables::SurfaceFlingerLayersSnapshotTable::Name();
  const dataframe::Dataframe* static_table_from_engine =
      engine_->GetDataframeOrNull(table_name);
  if (!static_table_from_engine) {
    return OnFailure(base::ErrStatus("Failed to find %s table.",
                                     std::string(table_name).c_str()));
  }

  table_.Clear();

  base::Status status =
      InsertRows(*static_table_from_engine, &table_, string_pool_);
  if (!status.ok()) {
    return OnFailure(status);
  }
  return OnSuccess(&table_.dataframe());
}

WinscopeSurfaceFlingerHierarchyPaths::WinscopeSurfaceFlingerHierarchyPaths(
    StringPool* string_pool,
    const PerfettoSqlEngine* engine)
    : string_pool_(string_pool), engine_(engine) {}

std::unique_ptr<StaticTableFunction::Cursor>
WinscopeSurfaceFlingerHierarchyPaths::MakeCursor() {
  return std::make_unique<Cursor>(string_pool_, engine_);
}

dataframe::DataframeSpec WinscopeSurfaceFlingerHierarchyPaths::CreateSpec() {
  return tables::WinscopeSurfaceFlingerHierarchyPathTable::kSpec
      .ToUntypedDataframeSpec();
}

std::string WinscopeSurfaceFlingerHierarchyPaths::TableName() {
  return tables::WinscopeSurfaceFlingerHierarchyPathTable::Name();
}

uint32_t WinscopeSurfaceFlingerHierarchyPaths::GetArgumentCount() const {
  return 0;
}

}  // namespace perfetto::trace_processor
