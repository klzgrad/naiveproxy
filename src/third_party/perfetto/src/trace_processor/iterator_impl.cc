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

#include "src/trace_processor/iterator_impl.h"

#include "perfetto/base/time.h"
#include "perfetto/trace_processor/trace_processor_storage.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/scoped_db.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/trace_processor_impl.h"

namespace perfetto {
namespace trace_processor {

IteratorImpl::IteratorImpl(
    TraceProcessorImpl* trace_processor,
    base::StatusOr<PerfettoSqlEngine::ExecutionResult> result,
    uint32_t sql_stats_row)
    : trace_processor_(trace_processor),
      result_(std::move(result)),
      sql_stats_row_(sql_stats_row) {}

IteratorImpl::~IteratorImpl() {
  if (trace_processor_) {
    base::TimeNanos t_end = base::GetWallTimeNs();
    auto* sql_stats =
        trace_processor_.get()->context_.storage->mutable_sql_stats();
    sql_stats->RecordQueryEnd(sql_stats_row_, t_end.count());
  }
}

void IteratorImpl::RecordFirstNextInSqlStats() {
  base::TimeNanos t_first_next = base::GetWallTimeNs();
  auto* sql_stats =
      trace_processor_.get()->context_.storage->mutable_sql_stats();
  sql_stats->RecordQueryFirstNext(sql_stats_row_, t_first_next.count());
}

Iterator::Iterator(std::unique_ptr<IteratorImpl> iterator)
    : iterator_(std::move(iterator)) {}
Iterator::~Iterator() = default;

Iterator::Iterator(Iterator&&) noexcept = default;
Iterator& Iterator::operator=(Iterator&&) noexcept = default;

bool Iterator::Next() {
  return iterator_->Next();
}

SqlValue Iterator::Get(uint32_t col) {
  return iterator_->Get(col);
}

std::string Iterator::GetColumnName(uint32_t col) {
  return iterator_->GetColumnName(col);
}

uint32_t Iterator::ColumnCount() {
  return iterator_->ColumnCount();
}

base::Status Iterator::Status() {
  return iterator_->Status();
}

uint32_t Iterator::StatementCount() {
  return iterator_->StatementCount();
}

uint32_t Iterator::StatementWithOutputCount() {
  return iterator_->StatementCountWithOutput();
}

std::string Iterator::LastStatementSql() {
  return iterator_->LastStatementSql();
}

}  // namespace trace_processor
}  // namespace perfetto
