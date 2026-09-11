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

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/tree/tree.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/plugins/flamegraph/flamegraph.h"

namespace perfetto::trace_processor::flamegraph {
namespace {

struct Rng {
  uint64_t state = 42;
  uint32_t operator()() {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(state >> 33);
  }
};

template <typename T>
core::Tree::Column ColumnFromValues(const std::vector<T>& values) {
  auto column =
      core::Tree::Column::Create<T>(static_cast<uint32_t>(values.size()));
  memcpy(column.data.begin(), values.data(), values.size() * sizeof(T));
  return column;
}

core::Tree MakeInput(StringPool* pool, uint32_t rows) {
  constexpr uint32_t kNames = 10000;
  Rng rng;
  std::vector<StringPool::Id> name_table;
  for (uint32_t i = 0; i < kNames; ++i) {
    name_table.push_back(
        pool->InternString(base::StringView("frame_" + std::to_string(i))));
  }
  core::Tree tree;
  tree.row_count = rows;
  tree.parent = core::Slab<uint32_t>::Alloc(rows);
  std::vector<StringPool::Id> names;
  std::vector<int64_t> values;
  std::vector<int64_t> groupings;
  names.reserve(rows);
  values.reserve(rows);
  groupings.reserve(rows);
  std::vector<uint32_t> stack;
  for (uint32_t row = 0; row < rows; ++row) {
    if (!stack.empty() && rng() % 100 < 40) {
      stack.resize(stack.size() - (rng() % stack.size()));
    }
    tree.parent[row] = stack.empty() ? core::Tree::kNullParent : stack.back();
    names.push_back(name_table[rng() % kNames]);
    values.push_back(rng() % 3 == 0 ? rng() % 100 : 0);
    groupings.push_back(row);
    stack.push_back(row);
  }
  tree.names = {"name", "value", "grouping"};
  tree.columns.push_back(ColumnFromValues(names));
  tree.columns.push_back(ColumnFromValues(values));
  tree.columns.push_back(ColumnFromValues(groupings));
  return tree;
}

struct RunOptions {
  static RunOptions Filtered() {
    RunOptions options;
    options.filtered = true;
    return options;
  }
  static RunOptions Pivot() {
    RunOptions options;
    options.pivot = true;
    return options;
  }
  static RunOptions Aggregate() {
    RunOptions options;
    options.aggregate = true;
    return options;
  }
  static RunOptions SummaryAggregate() {
    RunOptions options;
    options.summary_aggregate = true;
    return options;
  }
  static RunOptions ConcatAggregate() {
    RunOptions options;
    options.concat_aggregate = true;
    return options;
  }
  static RunOptions NumericFilter() {
    RunOptions options;
    options.numeric_filter = true;
    return options;
  }

  bool filtered = false;
  bool pivot = false;
  bool aggregate = false;
  bool summary_aggregate = false;
  bool concat_aggregate = false;
  bool numeric_filter = false;
};

void Run(benchmark::State& state, Config::View view, RunOptions options = {}) {
  StringPool pool;
  uint32_t rows = static_cast<uint32_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    core::Tree input = MakeInput(&pool, rows);
    Config config(pool);
    config.view = view;
    config.name = &input.columns[0];
    config.value_columns.push_back(&input.columns[1]);
    if (options.aggregate) {
      config.aggregate_columns.push_back(
          {&input.columns[1], Config::Aggregate::kSum, "sum_value"});
    }
    if (options.summary_aggregate) {
      config.aggregate_columns.push_back(
          {&input.columns[2], Config::Aggregate::kOneOrSummary, "summary"});
    }
    if (options.concat_aggregate) {
      config.aggregate_columns.push_back({&input.columns[2],
                                          Config::Aggregate::kConcatWithComma,
                                          "concatenated"});
    }
    if (options.numeric_filter) {
      config.grouping_columns.push_back(&input.columns[2]);
      config.show_stack_filters.push_back(
          base::Regex::CreateOrCheck("^12345$"));
    }
    if (options.filtered) {
      config.show_stack_filters.push_back(
          base::Regex::CreateOrCheck("frame_1\\d\\d"));
      config.hide_frame_filters.push_back(
          base::Regex::CreateOrCheck("frame_2\\d\\d"));
    }
    if (options.pivot) {
      config.view_pattern = base::Regex::CreateOrCheck("frame_12");
    }
    state.ResumeTiming();
    auto output = Build(std::move(input), config);
    PERFETTO_CHECK(output.ok());
    benchmark::DoNotOptimize(output);
  }
  state.counters["frames/s"] = benchmark::Counter(
      static_cast<double>(rows), benchmark::Counter::kIsIterationInvariantRate);
}

void BM_FlamegraphTopDown(benchmark::State& state) {
  Run(state, Config::View(Config::TopDown{}));
}
BENCHMARK(BM_FlamegraphTopDown)->Arg(100000)->Arg(1000000);

void BM_FlamegraphTopDownFiltered(benchmark::State& state) {
  Run(state, Config::View(Config::TopDown{}), RunOptions::Filtered());
}
BENCHMARK(BM_FlamegraphTopDownFiltered)->Arg(100000)->Arg(1000000);

void BM_FlamegraphTopDownAggregate(benchmark::State& state) {
  Run(state, Config::View(Config::TopDown{}), RunOptions::Aggregate());
}
BENCHMARK(BM_FlamegraphTopDownAggregate)->Arg(100000)->Arg(1000000);

void BM_FlamegraphTopDownNumericFilter(benchmark::State& state) {
  Run(state, Config::View(Config::TopDown{}), RunOptions::NumericFilter());
}
BENCHMARK(BM_FlamegraphTopDownNumericFilter)->Arg(100000)->Arg(1000000);

void BM_FlamegraphBottomUp(benchmark::State& state) {
  Run(state, Config::View(Config::BottomUp{}));
}
BENCHMARK(BM_FlamegraphBottomUp)->Arg(100000)->Arg(1000000);

void BM_FlamegraphBottomUpAggregate(benchmark::State& state) {
  Run(state, Config::View(Config::BottomUp{}), RunOptions::Aggregate());
}
BENCHMARK(BM_FlamegraphBottomUpAggregate)->Arg(100000)->Arg(1000000);

void BM_FlamegraphTopDownSummaryAggregate(benchmark::State& state) {
  Run(state, Config::View(Config::TopDown{}), RunOptions::SummaryAggregate());
}
BENCHMARK(BM_FlamegraphTopDownSummaryAggregate)->Arg(100000)->Arg(1000000);

void BM_FlamegraphTopDownConcatAggregate(benchmark::State& state) {
  Run(state, Config::View(Config::TopDown{}), RunOptions::ConcatAggregate());
}
BENCHMARK(BM_FlamegraphTopDownConcatAggregate)->Arg(100000)->Arg(1000000);

void BM_FlamegraphPivot(benchmark::State& state) {
  Run(state, Config::View(Config::Pivot{}), RunOptions::Pivot());
}
BENCHMARK(BM_FlamegraphPivot)->Arg(100000)->Arg(1000000);

}  // namespace
}  // namespace perfetto::trace_processor::flamegraph
