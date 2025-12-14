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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/symbolize.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "src/profiling/symbolizer/llvm_symbolizer.h"
#include "src/profiling/symbolizer/llvm_symbolizer_c_api.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/types/symbolization_input.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor::perfetto_sql {
namespace {
// Symbolize is essentially just a sql interface to profiling::LlvmSymbolizer
// SymbolizeBatch. The function takes a pointer to SymbolizationInput, which is
// constructed by __intrinsic_symbolize_agg from a  table with the columns
// "file_name" "rel_pc" "mapping_id" "address" and then symbolizes each row
// using llvm_symbolizer and returns function_name, file_name, line_number,
// mapping_id, address.
// Currently includes mapping_id and address as a way to join back symbolization
// results to original data.
// This function should be used with the _callstack_frame_symbolize! macro in
// order to simpfly it usage.
struct Symbolize : public sqlite::Function<Symbolize> {
  static constexpr char kName[] = "__intrinsic_symbolize";
  static constexpr int kArgCount = 1;

  struct UserData {
    PerfettoSqlEngine* engine;
    StringPool* pool;
    profiling::LlvmSymbolizer symbolizer = profiling::LlvmSymbolizer();
  };

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == kArgCount);
    Symbolize::UserData* user_data = GetUserData(ctx);
    auto* input = sqlite::value::Pointer<SymbolizationInput>(
        argv[0], SymbolizationInput::kName);
    if (!input) {
      return;
    }

    std::vector<std::string> col_names{"function_name", "file_name",
                                       "line_number", "mapping_id", "address"};
    using CT = dataframe::AdhocDataframeBuilder::ColumnType;
    std::vector<CT> col_types{
        CT::kString, CT::kString, CT::kInt64, CT::kInt64, CT::kInt64,
    };
    dataframe::AdhocDataframeBuilder builder(col_names, user_data->pool,
                                             col_types);

    profiling::LlvmSymbolizer* symbolizer = &user_data->symbolizer;

    profiling::SymbolizationResultBatch result =
        symbolizer->SymbolizeBatch(input->requests);

    for (uint32_t i = 0; i < result.size(); ++i) {
      auto [mapping_id, address] = input->mapping_id_and_address[i];
      auto [frames, num_frames] = result.GetFramesForRequest(i);
      if (num_frames == 0) {
        builder.PushNull(0);
        builder.PushNull(1);
        builder.PushNull(2);
        builder.PushNonNullUnchecked(3, static_cast<int64_t>(mapping_id));
        builder.PushNonNullUnchecked(4, static_cast<int64_t>(address));
        continue;
      }
      for (uint32_t j = 0; j < num_frames; ++j) {
        const auto& frame = frames[j];
        builder.PushNonNullUnchecked(
            0, user_data->pool->InternString(frame.function_name));
        builder.PushNonNullUnchecked(
            1, user_data->pool->InternString(frame.file_name));
        builder.PushNonNullUnchecked(2,
                                     static_cast<int64_t>(frame.line_number));
        builder.PushNonNullUnchecked(3, static_cast<int64_t>(mapping_id));
        builder.PushNonNullUnchecked(4, static_cast<int64_t>(address));
      }
    }
    SQLITE_ASSIGN_OR_RETURN(ctx, auto df, std::move(builder).Build());
    sqlite::result::UniquePointer(
        ctx, std::make_unique<dataframe::Dataframe>(std::move(df)), "TABLE");
  }
};

}  // namespace

base::Status RegisterSymbolizeFunction(PerfettoSqlEngine& engine,
                                       StringPool* pool) {
  return engine.RegisterFunction<Symbolize>(
      std::make_unique<Symbolize::UserData>(
          Symbolize::UserData{&engine, pool}));
}

}  // namespace perfetto::trace_processor::perfetto_sql
