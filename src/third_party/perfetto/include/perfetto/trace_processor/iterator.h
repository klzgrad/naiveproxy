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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_ITERATOR_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_ITERATOR_H_

#include <stdint.h>

#include <memory>

#include "perfetto/base/export.h"
#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"

namespace perfetto {
namespace trace_processor {

class IteratorImpl;
class SqliteIteratorImpl;

// Iterator returning SQL rows satisfied by a query.
//
// Example usage:
// auto sql = "select name, ifnull(cat, "[NULL]") from slice";
// for (auto it = tp.ExecuteQuery(sql); it.Next();)
//   for (uint32_t i = 0; i < it.ColumnCount(); ++i) {
//     printf("%s ", it.Get(i).AsString());
//   }
//   printf("\n");
// }
class PERFETTO_EXPORT_COMPONENT Iterator {
 public:
  explicit Iterator(std::unique_ptr<IteratorImpl>);
  ~Iterator();

  Iterator(const Iterator&) = delete;
  Iterator& operator=(const Iterator&) = delete;

  Iterator(Iterator&&) noexcept;
  Iterator& operator=(Iterator&&) noexcept;

  // Forwards the iterator to the next result row and returns a boolean of
  // whether there is a next row. If this method returns false,
  // |Status()| should be called to check if there was an error. If
  // there was no error, this means the EOF was reached.
  bool Next();

  // Returns the value associated with the column |col|. Any call to
  // |Get()| must be preceded by a call to |Next()| returning
  // true. |col| must be less than the number returned by |ColumnCount()|.
  SqlValue Get(uint32_t col);

  // Returns the name of the column at index |col|. Can be called even before
  // calling |Next()|.
  std::string GetColumnName(uint32_t col);

  // Returns the number of columns in this iterator's query. Can be called
  // even before calling |Next()|.
  uint32_t ColumnCount();

  // Returns the number of statements in the provided SQL (including the final
  // statement which is iterated using this iterator). Comments and empty
  // statements are *not* counted i.e.
  // "SELECT 1; /* comment */; select 2;  -- comment"
  // returns 2 not 4.
  uint32_t StatementCount();

  // Returns the number of statements which produced output rows in the provided
  // SQL (including, potentially, the final statement which is iterated using
  // this iterator).
  // This value is guaranteed to be <= |StatementCount()|.
  uint32_t StatementWithOutputCount();

  // Returns the last executed statement SQL (including, potentially, the final
  // statement which is iterated using this iterator).
  std::string LastStatementSql();

  // Returns the status of the iterator.
  base::Status Status();

 private:
  friend class QueryResultSerializer;

  // This is to allow QueryResultSerializer, which is very perf sensitive, to
  // access the impl directly and avoid one extra function call for each cell.
  // It downcasts to the concrete |T| (the serializer is only ever fed local
  // SqliteIteratorImpl iterators) so the per-cell calls devirtualize.
  template <typename T = IteratorImpl>
  std::unique_ptr<T> take_impl() {
    sqlite_fast_path_ = nullptr;
    return std::unique_ptr<T>(static_cast<T*>(iterator_.release()));
  }

  // A PIMPL pattern is used to avoid leaking the dependencies on sqlite3.h and
  // other internal classes.
  std::unique_ptr<IteratorImpl> iterator_;

  // Non-owning alias of |iterator_| set if backing impl is
  // the local SqliteIteratorImpl. All methods should
  // above call through this when set, so the the fast path is fast.
  //
  // Null for other iterator types.
  SqliteIteratorImpl* sqlite_fast_path_ = nullptr;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_ITERATOR_H_
