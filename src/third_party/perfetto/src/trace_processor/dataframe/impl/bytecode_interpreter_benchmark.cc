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

#include <benchmark/benchmark.h>
#include <cstdint>
#include <random>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/impl/bytecode_interpreter.h"
#include "src/trace_processor/dataframe/impl/bytecode_interpreter_impl.h"  // IWYU pragma: keep
#include "src/trace_processor/dataframe/impl/bytecode_interpreter_test_utils.h"
#include "src/trace_processor/dataframe/impl/flex_vector.h"

namespace perfetto::trace_processor::dataframe::impl::bytecode {
namespace {

static void BM_BytecodeInterpreter_LinearFilterEqUint32(
    benchmark::State& state) {
  constexpr uint32_t kTableSize = 1024 * 1024;

  // Setup column
  FlexVector<uint32_t> col_data_vec;
  for (uint32_t i = 0; i < kTableSize; ++i) {
    col_data_vec.push_back(i % 256);
  }
  Column col{Storage{std::move(col_data_vec)}, NullStorage::NonNull{},
             Unsorted{}, HasDuplicates{}};
  Column* col_ptr = &col;

  // Setup interpreter
  std::string bytecode_str = R"(
    CastFilterValue<Uint32>: [fval_handle=FilterValue(0), write_register=Register(0), op=Op(0)]
    InitRange: [size=1048576, dest_register=Register(1)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(3), dest_span_register=Register(2)]
    LinearFilterEq<Uint32>: [col=0, filter_value_reg=Register(0), source_register=Register(1), update_register=Register(2)]
  )";

  StringPool spool;
  std::vector<dataframe::Index> indexes;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 4, &col_ptr,
                         indexes.data(), &spool);

  Fetcher fetcher;
  fetcher.value.push_back(int64_t(123));

  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_LinearFilterEqUint32);

static void BM_BytecodeInterpreter_LinearFilterEqString(
    benchmark::State& state) {
  constexpr uint32_t kTableSize = 1024 * 1024;

  // Setup column
  StringPool spool;
  FlexVector<StringPool::Id> col_data_vec;
  std::vector<std::string> string_values;
  for (uint32_t i = 0; i < 256; ++i) {
    string_values.push_back("string_" + std::to_string(i));
  }
  for (uint32_t i = 0; i < kTableSize; ++i) {
    col_data_vec.push_back(
        spool.InternString(base::StringView(string_values[i % 256])));
  }
  Column col{Storage{std::move(col_data_vec)}, NullStorage::NonNull{},
             Unsorted{}, HasDuplicates{}};
  Column* col_ptr = &col;

  // Setup interpreter
  std::string bytecode_str = R"(
    CastFilterValue<String>: [fval_handle=FilterValue(0), write_register=Register(0), op=Op(0)]
    InitRange: [size=1048576, dest_register=Register(1)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(3), dest_span_register=Register(2)]
    LinearFilterEq<String>: [col=0, filter_value_reg=Register(0), source_register=Register(1), update_register=Register(2)]
  )";

  std::vector<dataframe::Index> indexes;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 4, &col_ptr,
                         indexes.data(), &spool);

  Fetcher fetcher;
  fetcher.value.push_back("string_123");

  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_LinearFilterEqString);

}  // namespace

static void BM_BytecodeInterpreter_SortUint32(benchmark::State& state) {
  constexpr uint32_t kTableSize = 1024 * 1024;

  // Setup column
  FlexVector<uint32_t> col_data_vec;
  std::minstd_rand0 rnd(0);
  for (uint32_t i = 0; i < kTableSize; ++i) {
    col_data_vec.push_back(static_cast<uint32_t>(rnd()));
  }
  Column col{Storage{std::move(col_data_vec)}, NullStorage::NonNull{},
             Unsorted{}, HasDuplicates{}};
  Column* col_ptr = &col;

  // Setup interpreter
  std::string bytecode_str = R"(
    InitRange: [size=1048576, dest_register=Register(0)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(1), dest_span_register=Register(2)]
    Iota: [source_register=Register(0), update_register=Register(2)]
    AllocateRowLayoutBuffer: [buffer_size=4194304, dest_buffer_register=Register(3)]
    CopyToRowLayout<Uint32, NonNull>: [col=0, source_indices_register=Register(2), dest_buffer_register=Register(3), row_layout_offset=0, row_layout_stride=4, invert_copied_bits=0, popcount_register=Register(4294967295), rank_map_register=Register(4294967295)]
    SortRowLayout: [buffer_register=Register(3), total_row_stride=4, indices_register=Register(2)]
  )";

  StringPool spool;
  std::vector<dataframe::Index> indexes;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 4, &col_ptr,
                         indexes.data(), &spool);

  Fetcher fetcher;
  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_SortUint32);

static void BM_BytecodeInterpreter_SortString(benchmark::State& state) {
  constexpr uint32_t kTableSize = 1024 * 1024;

  // Setup column
  StringPool spool;
  FlexVector<StringPool::Id> col_data_vec;
  std::minstd_rand0 rnd(0);
  for (uint32_t i = 0; i < kTableSize; ++i) {
    uint32_t len = 5 + (rnd() % (32 - 6));
    std::string key;
    for (uint32_t j = 0; j < len; ++j) {
      key += static_cast<char>('a' + (rnd() % 26));
    }
    col_data_vec.push_back(spool.InternString(base::StringView(key)));
  }
  Column col{Storage{std::move(col_data_vec)}, NullStorage::NonNull{},
             Unsorted{}, HasDuplicates{}};
  Column* col_ptr = &col;

  // Setup interpreter
  std::string bytecode_str = R"(
    InitRange: [size=1048576, dest_register=Register(0)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(1), dest_span_register=Register(2)]
    Iota: [source_register=Register(0), update_register=Register(2)]
    InitRankMap: [dest_register=Register(3)]
    CollectIdIntoRankMap: [col=0, source_register=Register(2), rank_map_register=Register(3)]
    FinalizeRanksInMap: [update_register=Register(3)]
    AllocateRowLayoutBuffer: [buffer_size=4194304, dest_buffer_register=Register(4)]
    CopyToRowLayout<String, NonNull>: [col=0, source_indices_register=Register(2), dest_buffer_register=Register(4), row_layout_offset=0, row_layout_stride=4, invert_copied_bits=1, popcount_register=Register(4294967295), rank_map_register=Register(3)]
    SortRowLayout: [buffer_register=Register(4), total_row_stride=4, indices_register=Register(2)]
  )";

  std::vector<dataframe::Index> indexes;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 5, &col_ptr,
                         indexes.data(), &spool);

  Fetcher fetcher;
  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_SortString);

}  // namespace perfetto::trace_processor::dataframe::impl::bytecode
