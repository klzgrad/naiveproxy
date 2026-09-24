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

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "src/trace_processor/sqlite_iterator_impl.h"

namespace perfetto::trace_processor {

// Out-of-line anchor for the abstract base's vtable.
IteratorImpl::~IteratorImpl() = default;

Iterator::Iterator(std::unique_ptr<IteratorImpl> iterator)
    : iterator_(std::move(iterator)),
      sqlite_fast_path_(iterator_ ? iterator_->AsSqlite() : nullptr) {}
Iterator::~Iterator() = default;

Iterator::Iterator(Iterator&&) noexcept = default;
Iterator& Iterator::operator=(Iterator&&) noexcept = default;

bool Iterator::Next() {
  return PERFETTO_LIKELY(sqlite_fast_path_) ? sqlite_fast_path_->Next()
                                            : iterator_->Next();
}

SqlValue Iterator::Get(uint32_t col) {
  return PERFETTO_LIKELY(sqlite_fast_path_) ? sqlite_fast_path_->Get(col)
                                            : iterator_->Get(col);
}

std::string Iterator::GetColumnName(uint32_t col) {
  return PERFETTO_LIKELY(sqlite_fast_path_)
             ? sqlite_fast_path_->GetColumnName(col)
             : iterator_->GetColumnName(col);
}

uint32_t Iterator::ColumnCount() {
  return PERFETTO_LIKELY(sqlite_fast_path_) ? sqlite_fast_path_->ColumnCount()
                                            : iterator_->ColumnCount();
}

base::Status Iterator::Status() {
  return PERFETTO_LIKELY(sqlite_fast_path_) ? sqlite_fast_path_->Status()
                                            : iterator_->Status();
}

uint32_t Iterator::StatementCount() {
  return PERFETTO_LIKELY(sqlite_fast_path_)
             ? sqlite_fast_path_->StatementCount()
             : iterator_->StatementCount();
}

uint32_t Iterator::StatementWithOutputCount() {
  return PERFETTO_LIKELY(sqlite_fast_path_)
             ? sqlite_fast_path_->StatementCountWithOutput()
             : iterator_->StatementCountWithOutput();
}

std::string Iterator::LastStatementSql() {
  return PERFETTO_LIKELY(sqlite_fast_path_)
             ? sqlite_fast_path_->LastStatementSql()
             : iterator_->LastStatementSql();
}

}  // namespace perfetto::trace_processor
