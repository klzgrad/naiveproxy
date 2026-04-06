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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/trees/tree_conversion.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/core/common/value_fetcher.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/runtime_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/tree/tree_transformer.h"
#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

namespace {

// Fetcher for all columns from SQLite argv.
// argv layout: [id_name, id_value, parent_id_name, parent_id_value,
//               col0_name, col0_value, col1_name, col1_value, ...]
struct SqliteArgvFetcher : core::ValueFetcher {
  using Type = sqlite::Type;
  static constexpr Type kInt64 = sqlite::Type::kInteger;
  static constexpr Type kDouble = sqlite::Type::kFloat;
  static constexpr Type kString = sqlite::Type::kText;
  static constexpr Type kNull = sqlite::Type::kNull;
  static constexpr Type kBytes = sqlite::Type::kBlob;

  Type GetValueType(uint32_t index) const {
    return sqlite::value::Type(argv[(2 * index) + 1]);
  }
  int64_t GetInt64Value(uint32_t index) const {
    return sqlite::value::Int64(argv[(2 * index) + 1]);
  }
  double GetDoubleValue(uint32_t index) const {
    return sqlite::value::Double(argv[(2 * index) + 1]);
  }
  const char* GetStringValue(uint32_t index) const {
    return sqlite::value::Text(argv[(2 * index) + 1]);
  }
  sqlite3_value** argv = nullptr;
};

struct AggCtx : sqlite::AggregateContext<AggCtx> {
  std::optional<core::dataframe::RuntimeDataframeBuilder> builder;
};

}  // namespace

void TreeFromTable::Step(sqlite3_context* ctx,
                         int rargc,
                         sqlite3_value** argv) {
  auto argc = static_cast<uint32_t>(rargc);
  auto& agg = AggCtx::GetOrCreateContextForStep(ctx);
  if (PERFETTO_UNLIKELY(!agg.builder)) {
    if (PERFETTO_UNLIKELY(argc < 4 || argc % 2 != 0)) {
      return sqlite::result::Error(
          ctx, "tree_from_table: incorrect argument layout");
    }
    uint32_t num_cols = argc / 2;
    std::vector<std::string> col_names;
    col_names.reserve(num_cols);
    for (uint32_t i = 0; i < argc; i += 2) {
      SQLITE_ASSIGN_OR_RETURN(
          ctx, auto col_name,
          sqlite::utils::ExtractArgument(argc, argv, "column name", i,
                                         SqlValue::Type::kString));
      col_names.emplace_back(col_name.AsString());
    }
    agg.builder = core::dataframe::RuntimeDataframeBuilder{
        std::move(col_names),
        GetUserData(ctx),
        {{}, core::dataframe::NullabilityType::kDenseNull},
    };
  }
  SqliteArgvFetcher fetcher{{}, argv};
  if (!agg.builder->AddRow(&fetcher)) {
    return sqlite::utils::SetError(ctx, agg.builder->status());
  }
}

void TreeFromTable::Final(sqlite3_context* ctx) {
  auto raw_agg = AggCtx::GetContextOrNullForFinal(ctx);
  if (PERFETTO_UNLIKELY(!raw_agg)) {
    return sqlite::utils::ReturnNullFromFunction(ctx);
  }
  auto& agg = *raw_agg.get();
  PERFETTO_CHECK(agg.builder);
  SQLITE_ASSIGN_OR_RETURN(ctx, auto df, std::move(*agg.builder).Build());
  return sqlite::result::UniquePointer(
      ctx,
      std::make_unique<sqlite::utils::MovePointer<tree::TreeTransformer>>(
          tree::TreeTransformer(std::move(df), GetUserData(ctx))),
      "TREE_TRANSFORMER");
}

void TreeToTable::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  if (argc != 1) {
    return sqlite::result::Error(ctx,
                                 "tree_to_table: expected exactly 1 argument");
  }
  auto* tree_ptr =
      sqlite::value::Pointer<sqlite::utils::MovePointer<tree::TreeTransformer>>(
          argv[0], "TREE_TRANSFORMER");
  if (!tree_ptr) {
    return sqlite::result::Error(ctx,
                                 "tree_to_table: expected TREE_TRANSFORMER");
  }
  if (tree_ptr->taken()) {
    return sqlite::result::Error(
        ctx, "tree_to_table: tree has already been consumed");
  }
  auto transformer = tree_ptr->Take();
  SQLITE_ASSIGN_OR_RETURN(ctx, auto df, std::move(transformer).ToDataframe());
  return sqlite::result::UniquePointer(
      ctx, std::make_unique<dataframe::Dataframe>(std::move(df)), "TABLE");
}

}  // namespace perfetto::trace_processor
