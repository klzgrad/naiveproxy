/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/sqlite_iterator_impl.h"

#include <cstdint>
#include <utility>

#include "perfetto/base/time.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/trace_processor_impl.h"

namespace perfetto::trace_processor {

SqliteIteratorImpl::SqliteIteratorImpl(
    TraceProcessorImpl* trace_processor,
    base::StatusOr<PerfettoSqlConnection::ExecutionResult> result,
    uint32_t sql_stats_row)
    : trace_processor_(trace_processor),
      result_(std::move(result)),
      sql_stats_row_(sql_stats_row) {}

SqliteIteratorImpl::~SqliteIteratorImpl() {
  if (trace_processor_) {
    base::TimeNanos t_end = base::GetWallTimeNs();
    auto* sql_stats =
        trace_processor_.get()->context()->storage->mutable_sql_stats();
    sql_stats->RecordQueryEnd(sql_stats_row_, t_end.count());
  }
}

void SqliteIteratorImpl::RecordFirstNextInSqlStats() {
  base::TimeNanos t_first_next = base::GetWallTimeNs();
  auto* sql_stats =
      trace_processor_.get()->context()->storage->mutable_sql_stats();
  sql_stats->RecordQueryFirstNext(sql_stats_row_, t_first_next.count());
}

}  // namespace perfetto::trace_processor
