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

#ifndef SRC_TRACE_PROCESSOR_CORE_COMMON_SORT_TYPES_H_
#define SRC_TRACE_PROCESSOR_CORE_COMMON_SORT_TYPES_H_

#include <cstdint>

#include "src/trace_processor/core/util/type_set.h"

namespace perfetto::trace_processor::core {

// Represents a column sorted by its id property.
// This is a special state that should only be applied to Id columns, indicating
// the natural ordering where indices equal values.
struct IdSorted {};

// Represents a column which has two properties:
// 1) is sorted in ascending order
// 2) for each unique value `v` in the column, the first occurrence of `v` is
//    at index `v` in the column.
//
// In essence, this means that the columns end up looking like:
// [0, 0, 0, 3, 3, 5, 5, 7, 7, 7, 10]
//
// This state can only be applied to Uint32 columns.
struct SetIdSorted {};

// Represents a column which is sorted in ascending order by its value.
struct Sorted {};

// Represents a column which is not sorted.
struct Unsorted {};

// TypeSet of all possible column sort states.
using SortState = core::TypeSet<IdSorted, SetIdSorted, Sorted, Unsorted>;

// Defines the direction for sorting.
enum class SortDirection : uint32_t {
  kAscending,
  kDescending,
};

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_COMMON_SORT_TYPES_H_
