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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_TIME_FUNCTIONS_VALUE_AT_MAX_TS_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_TIME_FUNCTIONS_VALUE_AT_MAX_TS_H_

#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"

namespace perfetto::trace_processor {

// VALUE_AT_MAX_TS(ts, value) aggregate: returns the value paired with the
// maximum ts seen across all rows.
class ValueAtMaxTs : public sqlite::AggregateFunction<ValueAtMaxTs> {
 public:
  static constexpr char kName[] = "VALUE_AT_MAX_TS";
  static constexpr int kArgCount = 2;
  struct Context {
    bool initialized;
    int value_type;

    int64_t max_ts;
    int64_t int_value_at_max_ts;
    double double_value_at_max_ts;
  };

  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
  static void Final(sqlite3_context* ctx);
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_TIME_FUNCTIONS_VALUE_AT_MAX_TS_H_
