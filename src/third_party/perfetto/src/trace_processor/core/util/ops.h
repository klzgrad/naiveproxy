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

#ifndef SRC_TRACE_PROCESSOR_CORE_UTIL_OPS_H_
#define SRC_TRACE_PROCESSOR_CORE_UTIL_OPS_H_

#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::ops {

// Copies rows selected by |source_rows| into a dense output. Exact in-place
// operation is supported when source_rows[i] >= i. Partial overlap is not
// supported. Keep this pure-copy loop in the header: function and alias-check
// overhead is measurable when Dataframe gathers small columns.
template <typename T>
void GatherRows(Span<const T> source,
                Span<T> output,
                Span<const uint32_t> source_rows) {
  PERFETTO_DCHECK(output.size() >= source_rows.size());
  const bool in_place = source.b == output.b;
  for (uint32_t row = 0; row < source_rows.size(); ++row) {
    PERFETTO_DCHECK(source_rows[row] < source.size());
    PERFETTO_DCHECK(!in_place || source_rows[row] >= row);
    output[row] = source[source_rows[row]];
  }
}

// Gathers dense nullable rows. The value spans and null bitvectors can each
// alias their corresponding input exactly when source_rows[i] >= i. Partial
// overlap is not supported.
template <typename T>
void GatherNullableRows(Span<const T> source,
                        const BitVector& source_non_null,
                        Span<T> output,
                        BitVector* output_non_null,
                        Span<const uint32_t> source_rows) {
  PERFETTO_DCHECK(output.size() >= source_rows.size());
  PERFETTO_DCHECK(output_non_null);
  PERFETTO_DCHECK(output_non_null->size() >= source_rows.size());
  const bool values_in_place = source.b == output.b;
  const bool nulls_in_place = &source_non_null == output_non_null;
  for (uint32_t row = 0; row < source_rows.size(); ++row) {
    const uint32_t source_row = source_rows[row];
    PERFETTO_DCHECK(source_row < source.size());
    PERFETTO_DCHECK((!values_in_place && !nulls_in_place) || source_row >= row);
    const bool non_null = source_non_null.is_set(source_row);
    if (non_null) {
      output[row] = source[source_row];
    }
    if (nulls_in_place) {
      output_non_null->change(row, non_null);
    } else {
      output_non_null->change_assume_unset(row, non_null);
    }
  }
}

// Estimates the number of distinct values using a bounded strided sample.
template <typename T>
uint32_t EstimateDistinctCount(base::FlatHashMap<int64_t, uint32_t>* counts,
                               Span<const T> values);

// Removes duplicate fixed-width rows and compacts |indices| in place. Rows in
// |row_layout| are parallel to indices rather than addressed by their values.
void DistinctRows(Span<const uint8_t> row_layout,
                  uint32_t row_stride,
                  Span<uint32_t>* indices);

// Stably sorts indices by fixed-width rows in |row_layout|.
void SortRowLayout(Span<const uint8_t> row_layout,
                   uint32_t row_stride,
                   Span<uint32_t>* indices);

#define PERFETTO_DECLARE_DISTINCT(type)                 \
  extern template uint32_t EstimateDistinctCount<type>( \
      base::FlatHashMap<int64_t, uint32_t>*, Span<const type>);
PERFETTO_DECLARE_DISTINCT(uint32_t)
PERFETTO_DECLARE_DISTINCT(int32_t)
PERFETTO_DECLARE_DISTINCT(int64_t)
PERFETTO_DECLARE_DISTINCT(StringPool::Id)
#undef PERFETTO_DECLARE_DISTINCT

}  // namespace perfetto::trace_processor::core::ops

#endif  // SRC_TRACE_PROCESSOR_CORE_UTIL_OPS_H_
