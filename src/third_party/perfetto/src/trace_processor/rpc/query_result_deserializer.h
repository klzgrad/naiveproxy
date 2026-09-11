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

#ifndef SRC_TRACE_PROCESSOR_RPC_QUERY_RESULT_DESERIALIZER_H_
#define SRC_TRACE_PROCESSOR_RPC_QUERY_RESULT_DESERIALIZER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"

namespace perfetto::trace_processor {

// Decodes the QueryResult protos emitted by QueryResultSerializer back into
// cells: the inverse of QueryResultSerializer and the single decoder for the
// CellsBatch wire format, shared by the --remote client and the serializer
// test.
//
// A query result may arrive as several QueryResult messages. Feed each to
// AddMessage(), which appends that message's cells (row-major) and accumulates
// the column names, statement metadata, error and end-of-stream flag.
class QueryResultDeserializer {
 public:
  // A decoded cell that owns its string/blob payload; ToSqlValue() points into
  // it, so the Cell must outlive the returned SqlValue.
  struct Cell {
    SqlValue::Type type = SqlValue::kNull;
    int64_t long_value = 0;
    double double_value = 0;
    std::string bytes;  // string / blob payload.
    SqlValue ToSqlValue() const;
  };

  // Decodes one serialized QueryResult message, appending its cells to |out|.
  // Returns an error if the batch is malformed (the cell-type stream disagrees
  // with the packed per-type payloads).
  base::Status AddMessage(const uint8_t* data,
                          size_t size,
                          std::vector<Cell>* out);

  const std::vector<std::string>& column_names() const { return column_names_; }
  uint32_t statement_count() const { return statement_count_; }
  uint32_t statement_with_output_count() const {
    return statement_with_output_count_;
  }
  const std::string& last_statement_sql() const { return last_statement_sql_; }
  const std::string& error() const { return error_; }
  bool eof() const { return eof_; }

 private:
  std::vector<std::string> column_names_;
  uint32_t statement_count_ = 0;
  uint32_t statement_with_output_count_ = 0;
  std::string last_statement_sql_;
  std::string error_;
  bool eof_ = false;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_RPC_QUERY_RESULT_DESERIALIZER_H_
