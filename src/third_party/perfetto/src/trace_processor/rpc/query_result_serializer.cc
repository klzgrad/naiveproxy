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

#include "perfetto/ext/trace_processor/rpc/query_result_serializer.h"

#include <vector>

#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_processor/iterator_impl.h"

#include "protos/perfetto/trace_processor/trace_processor.pbzero.h"

namespace perfetto {
namespace trace_processor {

namespace {

namespace pu = ::protozero::proto_utils;
using BatchProto = protos::pbzero::QueryResult::CellsBatch;
using ResultProto = protos::pbzero::QueryResult;

// The reserved field in trace_processor.proto.
static constexpr uint32_t kPaddingFieldId = 7;

uint8_t MakeLenDelimTag(uint32_t field_num) {
  uint32_t tag = pu::MakeTagLengthDelimited(field_num);
  PERFETTO_DCHECK(tag <= 127);  // Must fit in one byte.
  return static_cast<uint8_t>(tag);
}

}  // namespace

QueryResultSerializer::QueryResultSerializer(Iterator iter)
    : iter_(iter.take_impl()), num_cols_(iter_->ColumnCount()) {}

QueryResultSerializer::~QueryResultSerializer() = default;

bool QueryResultSerializer::Serialize(std::vector<uint8_t>* buf) {
  const size_t slice = batch_split_threshold_ + 4096;
  protozero::HeapBuffered<protos::pbzero::QueryResult> result(slice, slice);
  bool has_more = Serialize(result.get());
  auto arr = result.SerializeAsArray();
  buf->insert(buf->end(), arr.begin(), arr.end());
  return has_more;
}

bool QueryResultSerializer::Serialize(protos::pbzero::QueryResult* res) {
  PERFETTO_CHECK(!eof_reached_);

  if (!did_write_metadata_) {
    SerializeMetadata(res);
    did_write_metadata_ = true;
  }

  // In case of an error we still want to go through SerializeBatch(). That will
  // write an empty batch with the EOF marker. Errors can happen also in the
  // middle of a query, not just before starting it.

  SerializeBatch(res);
  MaybeSerializeError(res);
  return !eof_reached_;
}

void QueryResultSerializer::SerializeBatch(protos::pbzero::QueryResult* res) {
  // The buffer is filled in this way:
  // - Append all the strings as we iterate through the results. The rationale
  //   is that strings are typically the largest part of the result and we want
  //   to avoid copying these.
  // - While iterating, buffer all other types of cells. They will be appended
  //   at the end of the batch, after the string payload is known.

  // Note: this function uses uint32_t instead of size_t because Wasm doesn't
  // have yet native 64-bit integers and this is perf-sensitive.

  const auto& writer = *res->stream_writer();
  auto* batch = res->add_batch();

  // Start the |string_cells|.
  auto* strings = batch->BeginNestedMessage<protozero::Message>(
      BatchProto::kStringCellsFieldNumber);

  // This keeps track of the overall size of the batch. It is used to decide if
  // we need to prematurely end the batch, even if the batch_split_threshold_ is
  // not reached. This is to guard against the degenerate case of appending a
  // lot of very large strings and ending up with an enormous batch.
  uint32_t approx_batch_size = 16;

  std::vector<uint8_t> cell_types(cells_per_batch_);

  // Varints and doubles are written on stack-based storage and appended later.
  protozero::PackedVarInt varints;
  protozero::PackedFixedSizeInt<double> doubles;

  // We write blobs on a temporary heap buffer and append it at the end. Blobs
  // are extremely rare, trying to avoid copies is not worth the complexity.
  std::vector<uint8_t> blobs;

  uint32_t cell_idx = 0;
  bool batch_full = false;

  for (;; ++cell_idx, ++col_) {
    // This branch is hit before starting each row. Note that iter_->Next() must
    // be called before iterating on a row. col_ is initialized at MAX_INT in
    // the constructor.
    if (col_ >= num_cols_) {
      col_ = 0;
      // If num_cols_ == 0 and the query didn't return any result (e.g. CREATE
      // TABLE) we should exit at this point. We still need to advance the
      // iterator via Next() otherwise the statement will have no effect.
      if (!iter_->Next())
        break;  // EOF or error.

      PERFETTO_DCHECK(num_cols_ > 0);
      // We need to guarantee that a batch contains whole rows. Before moving to
      // the next row, make sure that: (i) there is space for all the columns;
      // (ii) the batch didn't grow too much.
      if (cell_idx + num_cols_ > cells_per_batch_ ||
          approx_batch_size > batch_split_threshold_) {
        batch_full = true;
        break;
      }
    }

    auto value = iter_->Get(col_);
    uint8_t cell_type = BatchProto::CELL_INVALID;
    switch (value.type) {
      case SqlValue::Type::kNull: {
        cell_type = BatchProto::CELL_NULL;
        break;
      }
      case SqlValue::Type::kLong: {
        cell_type = BatchProto::CELL_VARINT;
        varints.Append(value.long_value);
        approx_batch_size += 4;  // Just a guess, doesn't need to be accurate.
        break;
      }
      case SqlValue::Type::kDouble: {
        cell_type = BatchProto::CELL_FLOAT64;
        approx_batch_size += sizeof(double);
        doubles.Append(value.double_value);
        break;
      }
      case SqlValue::Type::kString: {
        // Append the string to the one |string_cells| proto field, just use
        // \0 to separate each string. We are deliberately NOT emitting one
        // proto repeated field for each string. Doing so significantly slows
        // down parsing on the JS side (go/postmessage-benchmark).
        cell_type = BatchProto::CELL_STRING;
        uint32_t len_with_nul =
            static_cast<uint32_t>(strlen(value.string_value)) + 1;
        const char* str_begin = value.string_value;
        strings->AppendRawProtoBytes(str_begin, len_with_nul);
        approx_batch_size += len_with_nul + 4;  // 4 is a guess on the preamble.
        break;
      }
      case SqlValue::Type::kBytes: {
        // Each blob is stored as its own repeated proto field, unlike strings.
        // Blobs don't incur in text-decoding overhead (and are also rare).
        cell_type = BatchProto::CELL_BLOB;
        auto* src = static_cast<const uint8_t*>(value.bytes_value);
        uint32_t len = static_cast<uint32_t>(value.bytes_count);
        uint8_t preamble[16];
        uint8_t* preamble_end = &preamble[0];
        *(preamble_end++) = MakeLenDelimTag(BatchProto::kBlobCellsFieldNumber);
        preamble_end = pu::WriteVarInt(len, preamble_end);
        blobs.insert(blobs.end(), preamble, preamble_end);
        blobs.insert(blobs.end(), src, src + len);
        approx_batch_size += len + 4;  // 4 is a guess on the preamble size.
        break;
      }
    }

    PERFETTO_DCHECK(cell_type != BatchProto::CELL_INVALID);
    cell_types[cell_idx] = cell_type;
  }  // for (cell)

  // Backfill the string size.
  strings->Finalize();
  strings = nullptr;

  // Write the cells headers (1 byte per cell).
  if (cell_idx > 0) {
    batch->AppendBytes(BatchProto::kCellsFieldNumber, cell_types.data(),
                       cell_idx);
  }

  // Append the |varint_cells|, copying over the packed varint buffer.
  if (varints.size())
    batch->set_varint_cells(varints);

  // Append the |float64_cells|, copying over the packed fixed64 buffer. This is
  // appended at a 64-bit aligned offset, so that JS can access these by overlay
  // a TypedArray, without extra copies.
  const uint32_t doubles_size = static_cast<uint32_t>(doubles.size());
  if (doubles_size > 0) {
    uint8_t preamble[16];
    uint8_t* preamble_end = &preamble[0];
    *(preamble_end++) = MakeLenDelimTag(BatchProto::kFloat64CellsFieldNumber);
    preamble_end = pu::WriteVarInt(doubles_size, preamble_end);
    uint32_t preamble_size = static_cast<uint32_t>(preamble_end - &preamble[0]);

    // The byte after the preamble must start at a 64bit-aligned offset.
    // The padding needs to be > 1 Byte because of proto encoding.
    const uint32_t off =
        static_cast<uint32_t>(writer.written() + preamble_size);
    const uint32_t aligned_off = (off + 7) & ~7u;
    uint32_t padding = aligned_off - off;
    padding = padding == 1 ? 9 : padding;
    if (padding > 0) {
      uint8_t pad_buf[10];
      uint8_t* pad = pad_buf;
      *(pad++) = pu::MakeTagVarInt(kPaddingFieldId);
      for (uint32_t i = 0; i < padding - 2; i++)
        *(pad++) = 0x80;
      *(pad++) = 0;
      batch->AppendRawProtoBytes(pad_buf, static_cast<size_t>(pad - pad_buf));
    }
    batch->AppendRawProtoBytes(preamble, preamble_size);
    PERFETTO_CHECK(writer.written() % 8 == 0);
    batch->AppendRawProtoBytes(doubles.data(), doubles_size);
  }  // if (doubles_size > 0)

  // Append the blobs.
  if (blobs.size() > 0) {
    batch->AppendRawProtoBytes(blobs.data(), blobs.size());
  }

  // If this is the last batch, write the EOF field.
  if (!batch_full) {
    eof_reached_ = true;
    batch->set_is_last_batch(true);
  }

  // Finally backfill the size of the whole |batch| sub-message.
  batch->Finalize();
}

void QueryResultSerializer::MaybeSerializeError(
    protos::pbzero::QueryResult* res) {
  if (iter_->Status().ok())
    return;
  std::string err = iter_->Status().message();
  // Make sure the |error| field is always non-zero if the query failed, so
  // the client can tell some error happened.
  if (err.empty())
    err = "Unknown error";
  res->set_error(err);
}

void QueryResultSerializer::SerializeMetadata(
    protos::pbzero::QueryResult* res) {
  PERFETTO_DCHECK(!did_write_metadata_);
  for (uint32_t c = 0; c < num_cols_; c++)
    res->add_column_names(iter_->GetColumnName(c));
  res->set_statement_count(iter_->StatementCount());
  res->set_statement_with_output_count(iter_->StatementCountWithOutput());
  res->set_last_statement_sql(iter_->LastStatementSql());
}

}  // namespace trace_processor
}  // namespace perfetto
