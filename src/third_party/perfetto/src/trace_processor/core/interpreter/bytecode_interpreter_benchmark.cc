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
#include <string>
#include <utility>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/duplicate_types.h"
#include "src/trace_processor/core/common/sort_types.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/interpreter/bytecode_interpreter.h"
#include "src/trace_processor/core/interpreter/bytecode_interpreter_impl.h"  // IWYU pragma: keep
#include "src/trace_processor/core/interpreter/bytecode_interpreter_test_utils.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/util/flex_vector.h"
#include "src/trace_processor/core/util/slab.h"

namespace perfetto::trace_processor::core::interpreter {
namespace {

void BM_BytecodeInterpreter_LinearFilterEqUint32(benchmark::State& state) {
  constexpr uint32_t kTableSize = 1024 * 1024;

  // Setup column
  FlexVector<uint32_t> col_data_vec;
  for (uint32_t i = 0; i < kTableSize; ++i) {
    col_data_vec.push_back(i % 256);
  }
  dataframe::Column col{dataframe::Storage{std::move(col_data_vec)},
                        dataframe::NullStorage::NonNull{}, Unsorted{},
                        HasDuplicates{}};

  // Setup interpreter
  // Register layout:
  // R0: CastFilterValueResult (filter value)
  // R1: Range (source range)
  // R2: Span<uint32_t> (output indices)
  // R3: Slab<uint32_t> (backing storage for output)
  // R4: StoragePtr (column data pointer)
  // R5: Slab<uint32_t> (dummy popcount for NonNull)
  std::string bytecode_str = R"(
    CastFilterValue<Uint32>: [fval_handle=FilterValue(0), write_register=Register(0), op=Op(0)]
    InitRange: [size=1048576, dest_register=Register(1)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(3), dest_span_register=Register(2)]
    LinearFilterEq<Uint32>: [storage_register=Register(4), filter_value_reg=Register(0), popcount_register=Register(5), source_register=Register(1), update_register=Register(2)]
  )";

  StringPool spool;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 6, &spool);

  // Set up storage pointer in register
  StoragePtr storage_ptr{col.storage.unchecked_data<Uint32>(), Uint32{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(4), storage_ptr);
  interpreter.SetRegisterValue(WriteHandle<Slab<uint32_t>>(5),
                               Slab<uint32_t>::Alloc(0));

  Fetcher fetcher;
  fetcher.value.push_back(int64_t(123));

  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_LinearFilterEqUint32);

void BM_BytecodeInterpreter_LinearFilterEqString(benchmark::State& state) {
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
  dataframe::Column col{dataframe::Storage{std::move(col_data_vec)},
                        dataframe::NullStorage::NonNull{}, Unsorted{},
                        HasDuplicates{}};

  // Setup interpreter
  // Register layout:
  // R0: CastFilterValueResult (filter value)
  // R1: Range (source range)
  // R2: Span<uint32_t> (output indices)
  // R3: Slab<uint32_t> (backing storage for output)
  // R4: StoragePtr (column data pointer)
  // R5: Slab<uint32_t> (dummy popcount for NonNull)
  std::string bytecode_str = R"(
    CastFilterValue<String>: [fval_handle=FilterValue(0), write_register=Register(0), op=Op(0)]
    InitRange: [size=1048576, dest_register=Register(1)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(3), dest_span_register=Register(2)]
    LinearFilterEq<String>: [storage_register=Register(4), filter_value_reg=Register(0), popcount_register=Register(5), source_register=Register(1), update_register=Register(2)]
  )";

  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 6, &spool);

  // Set up storage pointer in register
  StoragePtr storage_ptr{col.storage.unchecked_data<String>(), String{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(4), storage_ptr);
  interpreter.SetRegisterValue(WriteHandle<Slab<uint32_t>>(5),
                               Slab<uint32_t>::Alloc(0));

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
  dataframe::Column col{dataframe::Storage{std::move(col_data_vec)},
                        dataframe::NullStorage::NonNull{}, Unsorted{},
                        HasDuplicates{}};

  // Setup interpreter
  // Register layout:
  // R0: Range (source range)
  // R1: Slab<uint32_t> (backing storage for indices)
  // R2: Span<uint32_t> (indices)
  // R3: Slab<uint8_t> (row layout buffer)
  // R4: StoragePtr (column data pointer)
  std::string bytecode_str = R"(
    InitRange: [size=1048576, dest_register=Register(0)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(1), dest_span_register=Register(2)]
    Iota: [source_register=Register(0), update_register=Register(2)]
    AllocateRowLayoutBuffer: [buffer_size=4194304, dest_buffer_register=Register(3)]
    CopyToRowLayout<Uint32, NonNull>: [storage_register=Register(4), null_bv_register=Register(4294967295), source_indices_register=Register(2), dest_buffer_register=Register(3), row_layout_offset=0, row_layout_stride=4, invert_copied_bits=0, popcount_register=Register(4294967295), rank_map_register=Register(4294967295)]
    SortRowLayout: [buffer_register=Register(3), total_row_stride=4, indices_register=Register(2)]
  )";

  StringPool spool;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 5, &spool);

  // Set up storage pointer in register
  StoragePtr storage_ptr{col.storage.unchecked_data<Uint32>(), Uint32{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(4), storage_ptr);

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
  dataframe::Column col{dataframe::Storage{std::move(col_data_vec)},
                        dataframe::NullStorage::NonNull{}, Unsorted{},
                        HasDuplicates{}};

  // Setup interpreter
  // Register layout:
  // R0: Range (source range)
  // R1: Slab<uint32_t> (backing storage for indices)
  // R2: Span<uint32_t> (indices)
  // R3: StringIdToRankMap (rank map)
  // R4: Slab<uint8_t> (row layout buffer)
  // R5: StoragePtr (column data pointer)
  std::string bytecode_str = R"(
    InitRange: [size=1048576, dest_register=Register(0)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(1), dest_span_register=Register(2)]
    Iota: [source_register=Register(0), update_register=Register(2)]
    InitRankMap: [dest_register=Register(3)]
    CollectIdIntoRankMap: [storage_register=Register(5), source_register=Register(2), rank_map_register=Register(3)]
    FinalizeRanksInMap: [update_register=Register(3)]
    AllocateRowLayoutBuffer: [buffer_size=4194304, dest_buffer_register=Register(4)]
    CopyToRowLayout<String, NonNull>: [storage_register=Register(5), null_bv_register=Register(4294967295), source_indices_register=Register(2), dest_buffer_register=Register(4), row_layout_offset=0, row_layout_stride=4, invert_copied_bits=1, popcount_register=Register(4294967295), rank_map_register=Register(3)]
    SortRowLayout: [buffer_register=Register(4), total_row_stride=4, indices_register=Register(2)]
  )";

  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 6, &spool);

  // Set up storage pointer in register
  StoragePtr storage_ptr{col.storage.unchecked_data<String>(), String{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(5), storage_ptr);

  Fetcher fetcher;
  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_SortString);

}  // namespace perfetto::trace_processor::core::interpreter
