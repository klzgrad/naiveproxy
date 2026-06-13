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
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <numeric>
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
#include "src/trace_processor/core/util/span.h"

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
  std::string bytecode_str = R"(
    CastFilterValue<Uint32>: [fval_handle=FilterValue(0), write_register=Register(0), op=NonNullOp(0)]
    InitRange: [size=1048576, dest_register=Register(1)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(3), dest_span_register=Register(2)]
    LinearFilterEq<Uint32>: [storage_register=Register(4), filter_value_reg=Register(0), source_register=Register(1), update_register=Register(2)]
  )";

  StringPool spool;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 5, &spool);

  // Set up storage pointer in register
  StoragePtr storage_ptr{col.storage.unchecked_data<Uint32>(), Uint32{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(4), storage_ptr);

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
  std::string bytecode_str = R"(
    CastFilterValue<String>: [fval_handle=FilterValue(0), write_register=Register(0), op=NonNullOp(0)]
    InitRange: [size=1048576, dest_register=Register(1)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(3), dest_span_register=Register(2)]
    LinearFilterEq<String>: [storage_register=Register(4), filter_value_reg=Register(0), source_register=Register(1), update_register=Register(2)]
  )";

  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 5, &spool);

  // Set up storage pointer in register
  StoragePtr storage_ptr{col.storage.unchecked_data<String>(), String{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(4), storage_ptr);

  Fetcher fetcher;
  fetcher.value.push_back("string_123");

  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_LinearFilterEqString);

// Benchmark for In<Uint32> with varying list sizes.
// Measures the combined cost of CastFilterValueList + In on each Execute().
void BM_BytecodeInterpreter_InUint32(benchmark::State& state) {
  auto list_size = static_cast<uint32_t>(state.range(0));
  constexpr uint32_t kTableSize = 1024 * 1024;

  // Setup column with values 0..1023 repeating.
  FlexVector<uint32_t> col_data_vec;
  for (uint32_t i = 0; i < kTableSize; ++i) {
    col_data_vec.push_back(i % 1024);
  }
  dataframe::Column col{dataframe::Storage{std::move(col_data_vec)},
                        dataframe::NullStorage::NonNull{}, Unsorted{},
                        HasDuplicates{}};

  // Register layout:
  // R0: CastFilterValueListResult (filter value list)
  // R1: Range (source range)
  // R2: Span<uint32_t> (output indices)
  // R3: Slab<uint32_t> (backing storage for output)
  // R4: StoragePtr (column data pointer)
  std::string bytecode_str = R"(
    CastFilterValueList<Uint32>: [fval_handle=FilterValue(0), write_register=Register(0), op=NonNullOp(0)]
    InitRange: [size=1048576, dest_register=Register(1)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(3), dest_span_register=Register(2)]
    Iota: [source_register=Register(1), update_register=Register(2)]
    FilterIn<Uint32, NonNull>: [storage_register=Register(4), null_bv_register=Register(4294967295), value_list_register=Register(0), index_register=Register(4294967295), source_range_register=Register(4294967295), source_register=Register(2), dest_register=Register(2)]
  )";

  StringPool spool;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 5, &spool);

  StoragePtr storage_ptr{col.storage.unchecked_data<Uint32>(), Uint32{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(4), storage_ptr);

  // Build fetcher with list_size values spread across 0..1023.
  Fetcher fetcher;
  for (uint32_t i = 0; i < list_size; ++i) {
    fetcher.value.push_back(int64_t(i * (1024 / list_size)));
  }

  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_InUint32)->Arg(5)->Arg(50)->Arg(500);

// Same benchmark but with Id type (exercises bitvector path).
void BM_BytecodeInterpreter_InId(benchmark::State& state) {
  auto list_size = static_cast<uint32_t>(state.range(0));
  constexpr uint32_t kTableSize = 1024 * 1024;

  dataframe::Column col{dataframe::Storage::Id{kTableSize},
                        dataframe::NullStorage::NonNull{}, Unsorted{},
                        HasDuplicates{}};

  // Register layout:
  // R0: CastFilterValueListResult
  // R1: Range (source range)
  // R2: Span<uint32_t> (output indices)
  // R3: Slab<uint32_t> (backing storage)
  // R4: StoragePtr (column data pointer)
  std::string bytecode_str = R"(
    CastFilterValueList<Id>: [fval_handle=FilterValue(0), write_register=Register(0), op=NonNullOp(0)]
    InitRange: [size=1048576, dest_register=Register(1)]
    AllocateIndices: [size=1048576, dest_slab_register=Register(3), dest_span_register=Register(2)]
    Iota: [source_register=Register(1), update_register=Register(2)]
    FilterIn<Id, NonNull>: [storage_register=Register(4), null_bv_register=Register(4294967295), value_list_register=Register(0), index_register=Register(4294967295), source_range_register=Register(4294967295), source_register=Register(2), dest_register=Register(2)]
  )";

  StringPool spool;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 5, &spool);

  StoragePtr storage_ptr{nullptr, Id{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(4), storage_ptr);

  Fetcher fetcher;
  for (uint32_t i = 0; i < list_size; ++i) {
    fetcher.value.push_back(int64_t(i * (kTableSize / list_size)));
  }

  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_BytecodeInterpreter_InId)->Arg(5)->Arg(50)->Arg(500);

// Returns true if BENCHMARK_CONSTANT_SWEEPING env var is set. These benchmarks
// are disabled by default since they sweep many (n, k) combinations and take a
// long time. They exist solely to determine the threshold constant for the
// binary-search-vs-linear-scan decision in FilterIn.
bool IsBenchmarkConstantSweeping() {
  return getenv("BENCHMARK_CONSTANT_SWEEPING") != nullptr;
}
bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

// Args: n (table size), k (IN-list size).
static void FilterInConstantSweepingArgs(benchmark::internal::Benchmark* b) {
  // Always register at least one small arg so the benchmark doesn't crash
  // when run without BENCHMARK_CONSTANT_SWEEPING.
  if (!IsBenchmarkConstantSweeping() || IsBenchmarkFunctionalOnly()) {
    b->Args({1024, 5});
    return;
  }
  for (int n : {1024, 65536, 1048576}) {
    for (int k : {5, 20, 50, 200, 500, 2000}) {
      if (k < n) {
        b->Args({n, k});
      }
    }
  }
}

// Helper: builds column data and a sorted permutation vector for the indexed
// FilterIn benchmarks.
struct IndexedFilterInSetup {
  FlexVector<uint32_t> col_data_vec;
  std::vector<uint32_t> perm;
  dataframe::Column col;
  Slab<uint32_t> perm_slab;

  static IndexedFilterInSetup Create(uint32_t n) {
    FlexVector<uint32_t> col_data;
    for (uint32_t i = 0; i < n; ++i) {
      col_data.push_back(i % 1024);
    }
    // Build a second copy for the column (FlexVector is non-copyable).
    FlexVector<uint32_t> col_data_copy;
    for (uint32_t i = 0; i < n; ++i) {
      col_data_copy.push_back(i % 1024);
    }
    dataframe::Column c{dataframe::Storage{std::move(col_data_copy)},
                        dataframe::NullStorage::NonNull{}, Unsorted{},
                        HasDuplicates{}};

    // Build sorted permutation vector.
    std::vector<uint32_t> p(n);
    std::iota(p.begin(), p.end(), 0u);
    const uint32_t* data = col_data.data();
    std::stable_sort(p.begin(), p.end(), [data](uint32_t a, uint32_t b) {
      return data[a] < data[b];
    });

    // Copy to a Slab for the register.
    Slab<uint32_t> slab = Slab<uint32_t>::Alloc(n);
    memcpy(slab.data(), p.data(), n * sizeof(uint32_t));

    return {std::move(col_data), std::move(p), std::move(c), std::move(slab)};
  }
};

// Benchmarks the indexed binary search path of FilterIn.
// Forces binary search by setting kFilterInLinearScanThreshold very high.
void BM_FilterIn_IndexedBinarySearch(benchmark::State& state) {
  auto n = static_cast<uint32_t>(state.range(0));
  auto k = static_cast<uint32_t>(state.range(1));
  auto setup = IndexedFilterInSetup::Create(n);

  // Register layout:
  // R0: CastFilterValueListResult
  // R1: Slab<uint32_t> (backing for dest)
  // R2: Span<uint32_t> (dest span)
  // R3: StoragePtr
  // R4: Span<uint32_t> (index permutation vector)
  std::string bytecode_str =
      R"(
    CastFilterValueList<Uint32>: [fval_handle=FilterValue(0), write_register=Register(0), op=NonNullOp(0)]
    AllocateIndices: [size=)" +
      std::to_string(n) +
      R"(, dest_slab_register=Register(1), dest_span_register=Register(2)]
    FilterIn<Uint32, NonNull>: [storage_register=Register(3), null_bv_register=Register(4294967295), value_list_register=Register(0), index_register=Register(4), source_range_register=Register(4294967295), source_register=Register(4294967295), dest_register=Register(2)]
  )";

  StringPool spool;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 5, &spool);

  StoragePtr storage_ptr{setup.col.storage.unchecked_data<Uint32>(), Uint32{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(3), storage_ptr);

  Span<uint32_t> perm_span(setup.perm_slab.data(), setup.perm_slab.data() + n);
  interpreter.SetRegisterValue(WriteHandle<Span<uint32_t>>(4), perm_span);

  Fetcher fetcher;
  for (uint32_t i = 0; i < k; ++i) {
    fetcher.value.push_back(static_cast<int64_t>(i * (1024 / k)));
  }

  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_FilterIn_IndexedBinarySearch)->Apply(FilterInConstantSweepingArgs);

// Benchmarks the non-indexed linear scan path of FilterIn on the same data
// that the binary search benchmark uses. This simulates what happens when the
// linear scan fallback is chosen: all N rows are scanned with hash lookup.
// Uses Iota to populate source indices 0..n, then FilterIn scans in-place.
void BM_FilterIn_IndexedLinearScan(benchmark::State& state) {
  auto n = static_cast<uint32_t>(state.range(0));
  auto k = static_cast<uint32_t>(state.range(1));
  auto setup = IndexedFilterInSetup::Create(n);

  // Register layout:
  // R0: CastFilterValueListResult
  // R1: Slab<uint32_t> (backing for span)
  // R2: Span<uint32_t> (source and dest, in-place filtering)
  // R3: StoragePtr
  // R4: Range (for Iota)
  std::string bytecode_str =
      R"(
    CastFilterValueList<Uint32>: [fval_handle=FilterValue(0), write_register=Register(0), op=NonNullOp(0)]
    InitRange: [size=)" +
      std::to_string(n) + R"(, dest_register=Register(4)]
    AllocateIndices: [size=)" +
      std::to_string(n) +
      R"(, dest_slab_register=Register(1), dest_span_register=Register(2)]
    Iota: [source_register=Register(4), update_register=Register(2)]
    FilterIn<Uint32, NonNull>: [storage_register=Register(3), null_bv_register=Register(4294967295), value_list_register=Register(0), index_register=Register(4294967295), source_range_register=Register(4294967295), source_register=Register(2), dest_register=Register(2)]
  )";

  StringPool spool;
  Interpreter<Fetcher> interpreter;
  interpreter.Initialize(ParseBytecodeToVec(bytecode_str), 5, &spool);

  StoragePtr storage_ptr{setup.col.storage.unchecked_data<Uint32>(), Uint32{}};
  interpreter.SetRegisterValue(WriteHandle<StoragePtr>(3), storage_ptr);

  Fetcher fetcher;
  for (uint32_t i = 0; i < k; ++i) {
    fetcher.value.push_back(static_cast<int64_t>(i * (1024 / k)));
  }

  for (auto _ : state) {
    interpreter.Execute(fetcher);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_FilterIn_IndexedLinearScan)->Apply(FilterInConstantSweepingArgs);

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
    CopyToRowLayout<Uint32, NonNull>: [storage_register=Register(4), null_bv_register=Register(4294967295), source_indices_register=Register(2), dest_buffer_register=Register(3), row_layout_offset=0, row_layout_stride=4, invert_copied_bits=0, rank_map_register=Register(4294967295)]
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
    CopyToRowLayout<String, NonNull>: [storage_register=Register(5), null_bv_register=Register(4294967295), source_indices_register=Register(2), dest_buffer_register=Register(4), row_layout_offset=0, row_layout_stride=4, invert_copied_bits=1, rank_map_register=Register(3)]
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
