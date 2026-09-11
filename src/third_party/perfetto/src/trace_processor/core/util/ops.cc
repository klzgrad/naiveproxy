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

#include "src/trace_processor/core/util/ops.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <unordered_set>

#include "perfetto/base/logging.h"
#include "src/trace_processor/core/util/sort.h"

namespace perfetto::trace_processor::core::ops {
namespace {

constexpr uint32_t kDistinctSampleRows = 10000;
constexpr uint32_t kStableSortCutoff = 4096;

int64_t DistinctKey(uint32_t value) {
  return static_cast<int64_t>(value);
}

int64_t DistinctKey(int32_t value) {
  return static_cast<int64_t>(value);
}

int64_t DistinctKey(int64_t value) {
  return value;
}

int64_t DistinctKey(StringPool::Id value) {
  return static_cast<int64_t>(value.raw_id());
}

struct SortToken {
  uint32_t index;
  uint32_t buffer_offset;
};

struct RowLayoutLess {
  bool operator()(const SortToken& left, const SortToken& right) const {
    return memcmp(buffer + left.buffer_offset, buffer + right.buffer_offset,
                  stride) < 0;
  }
  const uint8_t* buffer;
  uint32_t stride;
};

struct RowLayoutKey {
  const uint8_t* operator()(const SortToken& token) const {
    return buffer + token.buffer_offset;
  }
  const uint8_t* buffer;
};

}  // namespace

template <typename T>
uint32_t EstimateDistinctCount(base::FlatHashMap<int64_t, uint32_t>* counts,
                               Span<const T> values) {
  const uint64_t total = values.size();
  if (total == 0) {
    return 0;
  }
  const uint64_t stride =
      total <= kDistinctSampleRows ? 1 : total / kDistinctSampleRows;
  counts->Clear();
  uint64_t sample = 0;
  uint64_t singletons = 0;
  for (uint64_t row = 0; row < total; row += stride) {
    auto [count, inserted] = counts->Insert(DistinctKey(values.b[row]), 1u);
    if (inserted) {
      ++singletons;
    } else if (++*count == 2) {
      --singletons;
    }
    ++sample;
  }

  const uint64_t distinct = counts->size();
  const double denominator = static_cast<double>(sample - singletons) +
                             static_cast<double>(singletons) *
                                 static_cast<double>(sample) /
                                 static_cast<double>(total);
  const double estimate = denominator > 0
                              ? static_cast<double>(sample) *
                                    static_cast<double>(distinct) / denominator
                              : static_cast<double>(distinct);
  uint64_t result = static_cast<uint64_t>(estimate + 0.5);
  if (result < distinct) {
    result = distinct;
  }
  if (result > total) {
    result = total;
  }
  return static_cast<uint32_t>(result);
}

void DistinctRows(Span<const uint8_t> row_layout,
                  uint32_t row_stride,
                  Span<uint32_t>* indices) {
  PERFETTO_DCHECK(indices);
  if (indices->empty()) {
    return;
  }
  PERFETTO_DCHECK(row_layout.size() >= indices->size() * row_stride);
  std::unordered_set<std::string_view> seen_rows;
  seen_rows.reserve(indices->size());

  const uint8_t* row = row_layout.b;
  uint32_t* output = indices->b;
  for (const uint32_t* index = indices->b; index != indices->e; ++index) {
    std::string_view row_view(reinterpret_cast<const char*>(row), row_stride);
    *output = *index;
    output += seen_rows.insert(row_view).second;
    row += row_stride;
  }
  indices->e = output;
}

void SortRowLayout(Span<const uint8_t> row_layout,
                   uint32_t row_stride,
                   Span<uint32_t>* indices) {
  PERFETTO_DCHECK(indices);
  const uint32_t rows = static_cast<uint32_t>(indices->size());
  if (rows <= 1) {
    return;
  }
  PERFETTO_DCHECK(row_layout.size() >= indices->size() * row_stride);

  std::unique_ptr<SortToken[]> tokens(new SortToken[rows]);
  std::unique_ptr<SortToken[]> scratch;
  for (uint32_t row = 0; row < rows; ++row) {
    tokens[row] = SortToken{indices->b[row], row * row_stride};
  }

  SortToken* sorted;
  if (rows < kStableSortCutoff) {
    std::stable_sort(tokens.get(), tokens.get() + rows,
                     RowLayoutLess{row_layout.b, row_stride});
    sorted = tokens.get();
  } else {
    scratch.reset(new SortToken[rows]);
    std::unique_ptr<uint32_t[]> counts(new uint32_t[1 << 16]);
    sorted = RadixSort(tokens.get(), tokens.get() + rows, scratch.get(),
                       counts.get(), row_stride, RowLayoutKey{row_layout.b});
  }

  for (uint32_t row = 0; row < rows; ++row) {
    indices->b[row] = sorted[row].index;
  }
}

#define PERFETTO_INSTANTIATE_DISTINCT(type)      \
  template uint32_t EstimateDistinctCount<type>( \
      base::FlatHashMap<int64_t, uint32_t>*, Span<const type>);
PERFETTO_INSTANTIATE_DISTINCT(uint32_t)
PERFETTO_INSTANTIATE_DISTINCT(int32_t)
PERFETTO_INSTANTIATE_DISTINCT(int64_t)
PERFETTO_INSTANTIATE_DISTINCT(StringPool::Id)
#undef PERFETTO_INSTANTIATE_DISTINCT

}  // namespace perfetto::trace_processor::core::ops
