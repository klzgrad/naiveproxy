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

#ifndef SRC_TRACE_PROCESSOR_CORE_COMMON_NULL_TYPES_H_
#define SRC_TRACE_PROCESSOR_CORE_COMMON_NULL_TYPES_H_

#include "src/trace_processor/core/util/type_set.h"

namespace perfetto::trace_processor::core {

// Represents a column that doesn't contain NULL values.
struct NonNull {};

// Represents a column that contains NULL values with the storage only
// containing data for non-NULL values.
struct SparseNull {};

// Represents a column that contains NULL values with the storage only
// containing data for non-NULL values while still needing to access the
// non-null values in O(1) time at any time.
struct SparseNullWithPopcountAlways {};

// Represents a column that contains NULL values with the storage only
// containing data for non-NULL values while still needing to access the
// non-null values in O(1) time only until the dataframe is finalized.
struct SparseNullWithPopcountUntilFinalization {};

// Represents a column that contains NULL values with the storage containing
// data for all values (with undefined values at positions that would be NULL).
struct DenseNull {};

// TypeSet of all possible column nullability states.
using Nullability = core::TypeSet<NonNull,
                                  SparseNull,
                                  SparseNullWithPopcountAlways,
                                  SparseNullWithPopcountUntilFinalization,
                                  DenseNull>;

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_COMMON_NULL_TYPES_H_
