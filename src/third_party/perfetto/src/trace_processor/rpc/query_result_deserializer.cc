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

#include "src/trace_processor/rpc/query_result_deserializer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/basic_types.h"

#include "protos/perfetto/trace_processor/trace_processor.pbzero.h"

namespace perfetto::trace_processor {
namespace {

using protos::pbzero::QueryResult;
using CellsBatch = QueryResult::CellsBatch;

}  // namespace

SqlValue QueryResultDeserializer::Cell::ToSqlValue() const {
  SqlValue
      value{};  // Zero the union so a kNull value has a deterministic repr.
  value.type = type;
  switch (type) {
    case SqlValue::kNull:
      break;
    case SqlValue::kLong:
      value.long_value = long_value;
      break;
    case SqlValue::kDouble:
      value.double_value = double_value;
      break;
    case SqlValue::kString:
      value.string_value = bytes.c_str();
      break;
    case SqlValue::kBytes:
      value.bytes_value = bytes.data();
      value.bytes_count = bytes.size();
      break;
  }
  return value;
}

base::Status QueryResultDeserializer::AddMessage(const uint8_t* data,
                                                 size_t size,
                                                 std::vector<Cell>* out) {
  QueryResult::Decoder qr(data, size);
  // Metadata appears only in the first message.
  for (auto it = qr.column_names(); it; ++it)
    column_names_.push_back(it->as_std_string());
  if (qr.has_statement_count())
    statement_count_ = qr.statement_count();
  if (qr.has_statement_with_output_count())
    statement_with_output_count_ = qr.statement_with_output_count();
  if (qr.has_last_statement_sql())
    last_statement_sql_ = qr.last_statement_sql().ToStdString();
  if (qr.has_error() && qr.error().size > 0)
    error_ = qr.error().ToStdString();

  // The serializer writes each cell type to its own packed column and a tag
  // stream (cells()) that indexes into them in order. Decode the columns, then
  // walk the tag stream consuming one entry per typed cell.
  std::vector<int64_t> varints;
  std::vector<double> doubles;
  std::vector<std::string> blobs;
  std::vector<std::string> strings;
  for (auto batch_it = qr.batch(); batch_it; ++batch_it) {
    protozero::ConstBytes batch_bytes = batch_it->as_bytes();
    CellsBatch::Decoder batch(batch_bytes.data, batch_bytes.size);

    varints.clear();
    doubles.clear();
    blobs.clear();
    strings.clear();
    bool parse_error = false;
    for (auto it = batch.varint_cells(&parse_error); it; ++it)
      varints.push_back(*it);
    for (auto it = batch.float64_cells(&parse_error); it; ++it)
      doubles.push_back(*it);
    for (auto it = batch.blob_cells(); it; ++it)
      blobs.push_back((*it).ToStdString());

    // string_cells is a single '\0'-joined blob; split it back into cells.
    protozero::ConstChars merged = batch.string_cells();
    for (size_t pos = 0; pos < merged.size;) {
      const void* nul = memchr(merged.data + pos, '\0', merged.size - pos);
      size_t end =
          nul ? static_cast<size_t>(static_cast<const char*>(nul) - merged.data)
              : merged.size;
      strings.emplace_back(merged.data + pos, end - pos);
      pos = nul ? end + 1 : merged.size;
    }

    size_t vi = 0, di = 0, bi = 0, si = 0;
    for (auto it = batch.cells(&parse_error); it; ++it) {
      Cell cell;
      switch (static_cast<uint8_t>(*it)) {
        case CellsBatch::CELL_INVALID:
        case CellsBatch::CELL_NULL:
          cell.type = SqlValue::kNull;
          break;
        case CellsBatch::CELL_VARINT:
          if (vi >= varints.size())
            return base::ErrStatus("Malformed query result: varint underflow");
          cell.type = SqlValue::kLong;
          cell.long_value = varints[vi++];
          break;
        case CellsBatch::CELL_FLOAT64:
          if (di >= doubles.size())
            return base::ErrStatus("Malformed query result: float64 underflow");
          cell.type = SqlValue::kDouble;
          cell.double_value = doubles[di++];
          break;
        case CellsBatch::CELL_STRING:
          if (si >= strings.size())
            return base::ErrStatus("Malformed query result: string underflow");
          cell.type = SqlValue::kString;
          cell.bytes = std::move(strings[si++]);
          break;
        case CellsBatch::CELL_BLOB:
          if (bi >= blobs.size())
            return base::ErrStatus("Malformed query result: blob underflow");
          cell.type = SqlValue::kBytes;
          cell.bytes = std::move(blobs[bi++]);
          break;
        default:
          return base::ErrStatus("Malformed query result: unknown cell type");
      }
      out->push_back(std::move(cell));
    }
    if (parse_error)
      return base::ErrStatus("Malformed query result: cell parse error");
    if (batch.is_last_batch())
      eof_ = true;
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
