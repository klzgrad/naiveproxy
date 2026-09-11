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

#ifndef SRC_TRACE_PROCESSOR_ITERATOR_IMPL_H_
#define SRC_TRACE_PROCESSOR_ITERATOR_IMPL_H_

#include <cstdint>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"

namespace perfetto::trace_processor {

class SqliteIteratorImpl;

// Abstract backing for the public Iterator class. Iterator owns a
// std::unique_ptr<IteratorImpl> and forwards each call to it.
//
// There are two implementations:
//   - SqliteIteratorImpl: the local iterator implementation.
//   - RemoteIteratorImpl: results received from a remote trace
//     processor over the RPC protocol.
class IteratorImpl {
 public:
  virtual ~IteratorImpl();

  // Methods called by the base Iterator class. See iterator.h for semantics.
  virtual bool Next() = 0;
  virtual SqlValue Get(uint32_t col) const = 0;
  virtual std::string GetColumnName(uint32_t col) const = 0;
  virtual base::Status Status() const = 0;
  virtual uint32_t ColumnCount() const = 0;
  virtual uint32_t StatementCount() const = 0;
  virtual uint32_t StatementCountWithOutput() const = 0;
  virtual std::string LastStatementSql() = 0;

  // Returns |this| if this is the local iterator implementation,
  // otherwise nullptr. Allows for fast-path optimizations.
  virtual SqliteIteratorImpl* AsSqlite() { return nullptr; }
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_ITERATOR_IMPL_H_
