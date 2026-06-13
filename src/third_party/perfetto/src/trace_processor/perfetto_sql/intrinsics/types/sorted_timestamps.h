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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_SORTED_TIMESTAMPS_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_SORTED_TIMESTAMPS_H_

#include <cstdint>
#include <vector>

namespace perfetto::trace_processor::perfetto_sql {

// A sorted collection of timestamps used as an intermediate type for the
// interval_create intrinsic function. Timestamps are collected via an
// aggregate function and must be passed in ascending order (via ORDER BY)
// to the scalar function.
struct SortedTimestamps {
  static constexpr char kName[] = "SORTED_TIMESTAMPS";
  std::vector<int64_t> timestamps;
};

}  // namespace perfetto::trace_processor::perfetto_sql

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_SORTED_TIMESTAMPS_H_
