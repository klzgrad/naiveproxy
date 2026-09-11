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

#include "src/trace_processor/core/tree/tree_from_dataframe.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"

namespace perfetto::trace_processor::core {
namespace {

enum class RowOrder {
  kParentFirst,
  kChildFirst,
  kMixed,
};

uint32_t MixedRowAt(uint32_t position, uint32_t rows) {
  return position % 2 == 0 ? position / 2 : rows - 1 - position / 2;
}

dataframe::AdhocDataframeBuilder BuildLongChain(uint32_t rows,
                                                RowOrder order,
                                                StringPool* pool) {
  std::vector<uint32_t> parent(rows, Tree::kNullParent);
  switch (order) {
    case RowOrder::kParentFirst:
      for (uint32_t row = 1; row < rows; ++row) {
        parent[row] = row - 1;
      }
      break;
    case RowOrder::kChildFirst:
      for (uint32_t row = 0; row + 1 < rows; ++row) {
        parent[row] = row + 1;
      }
      break;
    case RowOrder::kMixed:
      for (uint32_t position = 0; position + 1 < rows; ++position) {
        parent[MixedRowAt(position, rows)] = MixedRowAt(position + 1, rows);
      }
      break;
  }

  dataframe::AdhocDataframeBuilder::Options options;
  options.types = {dataframe::AdhocColumnType::kInt64,
                   dataframe::AdhocColumnType::kInt64};
  options.nullability_type = dataframe::NullabilityType::kDenseNull;
  dataframe::AdhocDataframeBuilder builder({"id", "parent_id"}, pool, options);
  for (uint32_t row = 0; row < rows; ++row) {
    PERFETTO_CHECK(builder.PushNonNull(0, static_cast<int64_t>(row)));
    if (parent[row] == Tree::kNullParent) {
      builder.PushNull(1);
    } else {
      PERFETTO_CHECK(builder.PushNonNull(1, static_cast<int64_t>(parent[row])));
    }
  }
  return builder;
}

void RunBuildTreeLongChain(benchmark::State& state, RowOrder order) {
  StringPool pool;
  const auto rows = static_cast<uint32_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    auto builder = BuildLongChain(rows, order, &pool);
    state.ResumeTiming();

    auto result = BuildTree(std::move(builder));
    if (!result.ok()) {
      state.SkipWithError(result.status().message().c_str());
      break;
    }
    benchmark::DoNotOptimize(result.value().row_count);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * rows);
}

void BM_BuildTreeParentFirstChain(benchmark::State& state) {
  RunBuildTreeLongChain(state, RowOrder::kParentFirst);
}
BENCHMARK(BM_BuildTreeParentFirstChain)->Arg(1000)->Arg(10000)->Arg(100000);

void BM_BuildTreeChildFirstChain(benchmark::State& state) {
  RunBuildTreeLongChain(state, RowOrder::kChildFirst);
}
BENCHMARK(BM_BuildTreeChildFirstChain)->Arg(1000)->Arg(10000)->Arg(100000);

void BM_BuildTreeMixedOrderChain(benchmark::State& state) {
  RunBuildTreeLongChain(state, RowOrder::kMixed);
}
BENCHMARK(BM_BuildTreeMixedOrderChain)->Arg(1000)->Arg(10000)->Arg(100000);

}  // namespace
}  // namespace perfetto::trace_processor::core
