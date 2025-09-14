/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/perf/perf_counter.h"

#include <cstdint>

#include "perfetto/base/logging.h"
#include "src/trace_processor/tables/counter_tables_py.h"

namespace perfetto::trace_processor::perf_importer {

void PerfCounter::AddDelta(int64_t ts, double delta) {
  last_count_ += delta;
  counter_table_.Insert({ts, track_id_, last_count_});
}

void PerfCounter::AddCount(int64_t ts, double count) {
  PERFETTO_CHECK(count >= last_count_);
  last_count_ = count;
  counter_table_.Insert({ts, track_id_, last_count_});
}

}  // namespace perfetto::trace_processor::perf_importer
