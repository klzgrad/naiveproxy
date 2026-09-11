// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/trace_processor/plugins/metadata/metadata.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {
namespace {

void ReportValue(sqlite3_context* ctx,
                 const TraceStorage* storage,
                 const tables::MetadataTable::ConstRowReference& rr) {
  if (rr.str_value().has_value()) {
    sqlite::result::StaticString(ctx,
                                 storage->GetString(*rr.str_value()).c_str());
  } else if (rr.int_value().has_value()) {
    sqlite::result::Long(ctx, *rr.int_value());
  } else {
    sqlite::result::Null(ctx);
  }
}

}  // namespace

// static
void ExtractMetadata::Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
  if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull) {
    return;
  }
  const char* name = sqlite::value::Text(argv[0]);
  auto* user_data = GetUserData(ctx);

  auto& cursor = user_data->cursor;
  cursor.SetFilterValueUnchecked(0, name);
  cursor.Execute();
  if (!cursor.Eof()) {
    ReportValue(ctx, user_data->storage,
                user_data->storage
                    ->metadata_table()[cursor.ToRowNumber().row_number()]);
  }
}

// static
void ExtractMetadataForMachine::Step(sqlite3_context* ctx,
                                     int,
                                     sqlite3_value** argv) {
  if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull ||
      sqlite::value::Type(argv[1]) == sqlite::Type::kNull) {
    return;
  }
  auto machine_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  const char* name = sqlite::value::Text(argv[1]);

  auto* user_data = GetUserData(ctx);
  auto& cursor = user_data->cursor;
  cursor.SetFilterValueUnchecked(0, name);
  cursor.SetFilterValueUnchecked(1, machine_id);
  cursor.Execute();
  if (!cursor.Eof()) {
    ReportValue(ctx, user_data->storage,
                user_data->storage
                    ->metadata_table()[cursor.ToRowNumber().row_number()]);
  }
}

// static
void ExtractMetadataForTrace::Step(sqlite3_context* ctx,
                                   int,
                                   sqlite3_value** argv) {
  if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull ||
      sqlite::value::Type(argv[1]) == sqlite::Type::kNull) {
    return;
  }
  auto trace_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  const char* name = sqlite::value::Text(argv[1]);

  auto* user_data = GetUserData(ctx);
  auto& cursor = user_data->cursor;
  cursor.SetFilterValueUnchecked(0, name);
  cursor.SetFilterValueUnchecked(1, trace_id);
  cursor.Execute();
  if (!cursor.Eof()) {
    ReportValue(ctx, user_data->storage,
                user_data->storage
                    ->metadata_table()[cursor.ToRowNumber().row_number()]);
  }
}

// static
void ExtractExactMetadata::Step(sqlite3_context* ctx,
                                int,
                                sqlite3_value** argv) {
  if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull ||
      sqlite::value::Type(argv[1]) == sqlite::Type::kNull ||
      sqlite::value::Type(argv[2]) == sqlite::Type::kNull) {
    return;
  }
  auto machine_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  auto trace_id = static_cast<uint32_t>(sqlite::value::Int64(argv[1]));
  const char* name = sqlite::value::Text(argv[2]);

  auto* user_data = GetUserData(ctx);
  auto& cursor = user_data->cursor;
  cursor.SetFilterValueUnchecked(0, name);
  cursor.SetFilterValueUnchecked(1, machine_id);
  cursor.SetFilterValueUnchecked(2, trace_id);
  cursor.Execute();
  if (!cursor.Eof()) {
    ReportValue(ctx, user_data->storage,
                user_data->storage
                    ->metadata_table()[cursor.ToRowNumber().row_number()]);
  }
}

namespace metadata {
namespace {

class MetadataPlugin : public Plugin<MetadataPlugin> {
 public:
  ~MetadataPlugin() override;
  void RegisterFunctions(PerfettoSqlConnection*,
                         std::vector<FunctionRegistration>& out) override {
    auto* s = trace_context_->storage.get();
    out.push_back(MakeFunctionRegistration<ExtractMetadata>(
        std::make_unique<ExtractMetadata::Context>(s)));
    out.push_back(MakeFunctionRegistration<ExtractMetadataForMachine>(
        std::make_unique<ExtractMetadataForMachine::Context>(s)));
    out.push_back(MakeFunctionRegistration<ExtractMetadataForTrace>(
        std::make_unique<ExtractMetadataForTrace::Context>(s)));
    out.push_back(MakeFunctionRegistration<ExtractExactMetadata>(
        std::make_unique<ExtractExactMetadata::Context>(s)));
  }
};
MetadataPlugin::~MetadataPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<MetadataPlugin>();
      },
      MetadataPlugin::kPluginId, MetadataPlugin::kDepIds.data(),
      MetadataPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace metadata
}  // namespace perfetto::trace_processor
