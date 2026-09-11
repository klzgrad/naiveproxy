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

#include "src/trace_processor/core/dataframe/arrow_serializer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/dataframe/arrow_internal.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/util/flatbuffer_writer.h"

namespace perfetto::trace_processor::core::dataframe {

struct ArrowSerializer::BodyPlan {
  uint32_t rows = 0;
  uint64_t size = 0;
  std::vector<arrow_internal::FieldNode> nodes;
  std::vector<arrow_internal::ArrowBuffer> buffers;
};

struct ArrowSerializer::DictionaryPlan {
  std::vector<StringPool::Id> values;
  base::FlatHashMap<uint32_t, uint32_t> index_by_id;
  uint32_t data_length = 0;
  uint64_t body_size = 0;
  std::vector<arrow_internal::ArrowBuffer> buffers;
  std::vector<uint8_t> message;
};

namespace {

using W = util::FlatBufferWriter;

using namespace arrow_internal;

base::StatusOr<ArrowBuffer> AddBuffer(uint64_t length, uint64_t* cursor) {
  constexpr uint64_t kMax =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  if (length > kMax - (kArrowAlignment - 1) ||
      *cursor > kMax - AlignToArrow(length)) {
    return base::ErrStatus("Dataframe is too large for an Arrow file");
  }
  ArrowBuffer result{static_cast<int64_t>(*cursor),
                     static_cast<int64_t>(length)};
  *cursor += AlignToArrow(length);
  return result;
}

const uint8_t* NumericData(const Storage& storage) {
  if (storage.type().Is<core::Double>()) {
    return reinterpret_cast<const uint8_t*>(storage.unchecked_data<Double>());
  }
  if (storage.type().Is<core::Int64>()) {
    return reinterpret_cast<const uint8_t*>(storage.unchecked_data<Int64>());
  }
  if (storage.type().Is<core::Int32>()) {
    return reinterpret_cast<const uint8_t*>(storage.unchecked_data<Int32>());
  }
  PERFETTO_CHECK(storage.type().Is<core::Uint32>());
  return reinterpret_cast<const uint8_t*>(storage.unchecked_data<Uint32>());
}

void Append(std::vector<uint8_t>* output, const void* data, size_t size) {
  if (size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    output->insert(output->end(), bytes, bytes + size);
  }
}

void AppendU32(std::vector<uint8_t>* output, uint32_t value) {
  Append(output, &value, sizeof(value));
}

void AppendMessage(std::vector<uint8_t>* output,
                   const std::vector<uint8_t>& metadata,
                   uint32_t padded_size) {
  MessagePrefix prefix{kContinuation, static_cast<int32_t>(padded_size)};
  Append(output, &prefix, sizeof(prefix));
  Append(output, metadata.data(), metadata.size());
  output->resize(output->size() + padded_size - metadata.size(), 0);
}

std::vector<uint8_t> FinishFlatbuffer(W* writer, W::Offset root) {
  writer->Finish(root);
  return std::vector<uint8_t>(writer->data(), writer->data() + writer->size());
}

base::Status Emit(const ArrowSerializer::WriteFn& write,
                  const void* data,
                  size_t size) {
  return size ? write(static_cast<const uint8_t*>(data), size)
              : base::OkStatus();
}

// Bounds temporary memory while still coalescing small writes. Existing dense
// buffers and large strings bypass the scratch buffer and go directly to the
// caller's sink.
class BufferedOutput {
 public:
  static constexpr size_t kChunkSize = 64 * 1024;

  BufferedOutput(FlexVector<uint8_t>* scratch,
                 const ArrowSerializer::WriteFn& write)
      : scratch_(scratch), write_(write) {
    scratch_->resize(kChunkSize);
  }

  base::Status Append(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    while (size) {
      if (!used_ && size >= kChunkSize) {
        return Emit(write_, bytes, size);
      }
      size_t copied = std::min(size, kChunkSize - used_);
      memcpy(scratch_->data() + used_, bytes, copied);
      used_ += copied;
      bytes += copied;
      size -= copied;
      if (used_ == kChunkSize) {
        RETURN_IF_ERROR(Flush());
      }
    }
    return base::OkStatus();
  }

  base::Status AppendPadding(size_t size) {
    PERFETTO_DCHECK(size < kArrowAlignment);
    const uint8_t zeros[kArrowAlignment] = {};
    return Append(zeros, size);
  }

  base::Status Finish() { return Flush(); }

 private:
  base::Status Flush() {
    RETURN_IF_ERROR(Emit(write_, scratch_->data(), used_));
    used_ = 0;
    return base::OkStatus();
  }

  FlexVector<uint8_t>* scratch_;
  const ArrowSerializer::WriteFn& write_;
  size_t used_ = 0;
};

int64_t CountNulls(uint32_t rows, const BitVector& validity) {
  PERFETTO_DCHECK(validity.size() == rows);
  return rows - static_cast<int64_t>(validity.CountSetBits());
}

// Maps Arrow's logical row index to dataframe's physical storage index. Dense
// columns use the logical row directly; sparse columns advance only for valid
// rows because null values have no storage slot.
class StorageIndexMapper {
 public:
  StorageIndexMapper(const BitVector* validity, bool sparse)
      : validity_(validity), sparse_(sparse) {}

  bool HasValue(uint32_t logical_row) const {
    return !validity_ || validity_->is_set(logical_row);
  }

  uint32_t Take(uint32_t logical_row) {
    PERFETTO_DCHECK(HasValue(logical_row));
    return sparse_ ? sparse_row_++ : logical_row;
  }

 private:
  const BitVector* validity_;
  bool sparse_;
  uint32_t sparse_row_ = 0;
};

// Small subset of the Arrow flatbuffer schema used by this file.
W::Offset BuildInt(W& w, int32_t width, bool is_signed) {
  w.StartTable();
  w.FieldI32(int_field::kBitWidth, width);
  w.FieldBool(int_field::kIsSigned, is_signed);
  return w.EndTable();
}

W::Offset BuildFloatingPoint(W& w) {
  w.StartTable();
  w.FieldI16(floating_point_field::kPrecision, kPrecisionDouble);
  return w.EndTable();
}

W::Offset BuildUtf8(W& w) {
  w.StartTable();
  return w.EndTable();
}

W::Offset BuildDictionaryEncoding(W& w, int64_t id) {
  auto index_type = BuildInt(w, kDictionaryIndexBits, true);
  w.StartTable();
  w.FieldI64(dictionary_encoding_field::kId, id);
  w.FieldOffset(dictionary_encoding_field::kIndexType, index_type);
  return w.EndTable();
}

W::Offset BuildField(W& w,
                     const std::string& name,
                     StorageType type,
                     bool nullable,
                     int64_t dictionary_id) {
  uint8_t arrow_type;
  W::Offset type_table;
  W::Offset dictionary;
  bool has_dictionary = type.Is<core::String>();
  if (type.Is<core::String>()) {
    arrow_type = kTypeUtf8;
    type_table = BuildUtf8(w);
    dictionary = BuildDictionaryEncoding(w, dictionary_id);
  } else if (type.Is<core::Double>()) {
    arrow_type = kTypeFloatingPoint;
    type_table = BuildFloatingPoint(w);
  } else {
    arrow_type = kTypeInt;
    type_table =
        BuildInt(w, static_cast<int32_t>(NumericSize(type) * kBitsPerByte),
                 !type.Is<core::Id>() && !type.Is<core::Uint32>());
  }
  auto name_offset = w.WriteString(name);
  w.StartTable();
  w.FieldOffset(field_field::kName, name_offset);
  w.FieldBool(field_field::kNullable, nullable);
  w.FieldU8(field_field::kTypeType, arrow_type);
  w.FieldOffset(field_field::kType, type_table);
  if (has_dictionary) {
    w.FieldOffset(field_field::kDictionary, dictionary);
  }
  return w.EndTable();
}

W::Offset BuildMessage(W& w,
                       uint8_t header_type,
                       W::Offset header,
                       int64_t body_length) {
  w.StartTable();
  w.FieldI16(message_field::kVersion, kMetadataV5);
  w.FieldU8(message_field::kHeaderType, header_type);
  w.FieldOffset(message_field::kHeader, header);
  w.FieldI64(message_field::kBodyLength, body_length);
  return w.EndTable();
}

W::Offset BuildRecordBatch(W& w,
                           uint32_t rows,
                           const std::vector<FieldNode>& nodes,
                           const std::vector<ArrowBuffer>& buffers) {
  auto buffer_vector =
      w.WriteVecStruct(buffers.data(), sizeof(ArrowBuffer),
                       static_cast<uint32_t>(buffers.size()), alignof(int64_t));
  auto node_vector =
      w.WriteVecStruct(nodes.data(), sizeof(FieldNode),
                       static_cast<uint32_t>(nodes.size()), alignof(int64_t));
  w.StartTable();
  w.FieldI64(record_batch_field::kLength, rows);
  w.FieldOffset(record_batch_field::kNodes, node_vector);
  w.FieldOffset(record_batch_field::kBuffers, buffer_vector);
  return w.EndTable();
}

W::Offset BuildDictionaryBatch(W& w,
                               int64_t id,
                               uint32_t length,
                               const std::vector<ArrowBuffer>& buffers) {
  std::vector<FieldNode> nodes{FieldNode{length, 0}};
  auto data = BuildRecordBatch(w, length, nodes, buffers);
  w.StartTable();
  w.FieldI64(dictionary_batch_field::kId, id);
  w.FieldOffset(dictionary_batch_field::kData, data);
  return w.EndTable();
}

W::Offset BuildFooter(W& w,
                      W::Offset schema,
                      const std::vector<Block>& dictionary_blocks,
                      const Block& block) {
  auto blocks = w.WriteVecStruct(&block, sizeof(block), kRecordBatchCount,
                                 alignof(int64_t));
  auto dictionaries = w.WriteVecStruct(
      dictionary_blocks.data(), sizeof(Block),
      static_cast<uint32_t>(dictionary_blocks.size()), alignof(int64_t));
  w.StartTable();
  w.FieldI16(footer_field::kVersion, kMetadataV5);
  w.FieldOffset(footer_field::kSchema, schema);
  w.FieldOffset(footer_field::kDictionaries, dictionaries);
  w.FieldOffset(footer_field::kRecordBatches, blocks);
  return w.EndTable();
}

base::Status WriteValidityBuffer(uint32_t rows,
                                 const BitVector& validity,
                                 FlexVector<uint8_t>* scratch,
                                 const ArrowSerializer::WriteFn& write) {
  size_t bytes = static_cast<size_t>(ValidityBufferSize(rows));
  BufferedOutput output(scratch, write);
  for (size_t byte_index = 0; byte_index < bytes; ++byte_index) {
    uint8_t bitmap = 0;
    uint32_t first_row = static_cast<uint32_t>(byte_index * kBitsPerByte);
    uint32_t rows_in_byte = std::min(kBitsPerByte, rows - first_row);
    uint32_t end_row = first_row + rows_in_byte;
    for (uint32_t row = first_row; row < end_row; ++row) {
      if (validity.is_set(row)) {
        bitmap |= BitmapMask(row);
      }
    }
    RETURN_IF_ERROR(output.Append(&bitmap, sizeof(bitmap)));
  }
  RETURN_IF_ERROR(
      output.AppendPadding(static_cast<size_t>(AlignToArrow(bytes)) - bytes));
  return output.Finish();
}

base::Status WriteUtf8OffsetBuffer(const std::vector<StringPool::Id>& values,
                                   const StringPool& pool,
                                   uint32_t data_length,
                                   BufferedOutput* output) {
  int32_t offset = 0;
  for (StringPool::Id id : values) {
    RETURN_IF_ERROR(output->Append(&offset, sizeof(offset)));
    offset += static_cast<int32_t>(pool.Get(id).size());
  }
  RETURN_IF_ERROR(output->Append(&offset, sizeof(offset)));
  PERFETTO_CHECK(static_cast<uint32_t>(offset) == data_length);
  size_t bytes = static_cast<size_t>(
      Utf8OffsetBufferSize(static_cast<uint32_t>(values.size())));
  return output->AppendPadding(static_cast<size_t>(AlignToArrow(bytes)) -
                               bytes);
}

base::Status WriteIdBuffer(uint32_t rows,
                           FlexVector<uint8_t>* scratch,
                           const ArrowSerializer::WriteFn& write) {
  BufferedOutput output(scratch, write);
  for (uint32_t row = 0; row < rows; ++row) {
    RETURN_IF_ERROR(output.Append(&row, sizeof(row)));
  }
  size_t bytes = static_cast<size_t>(rows) * sizeof(uint32_t);
  RETURN_IF_ERROR(
      output.AppendPadding(static_cast<size_t>(AlignToArrow(bytes)) - bytes));
  return output.Finish();
}

base::Status WriteNumericBuffer(uint32_t rows,
                                const Column& column,
                                bool sparse,
                                const BitVector* validity,
                                FlexVector<uint8_t>* scratch,
                                const ArrowSerializer::WriteFn& write) {
  size_t element_size = NumericSize(column.storage.type());
  size_t bytes = static_cast<size_t>(rows) * element_size;
  size_t padded = static_cast<size_t>(AlignToArrow(bytes));
  const uint8_t* source = NumericData(column.storage);
  const uint8_t zeros[kArrowAlignment] = {};
  if (!sparse) {
    RETURN_IF_ERROR(Emit(write, source, bytes));
    return Emit(write, zeros, padded - bytes);
  }

  // Sparse dataframe storage omits null rows. Arrow numeric arrays do not, so
  // scatter values while streaming logical rows through bounded scratch space.
  PERFETTO_DCHECK(validity);
  BufferedOutput output(scratch, write);
  StorageIndexMapper mapper(validity, sparse);
  for (uint32_t row = 0; row < rows; ++row) {
    if (mapper.HasValue(row)) {
      RETURN_IF_ERROR(output.Append(source + mapper.Take(row) * element_size,
                                    element_size));
    } else {
      RETURN_IF_ERROR(output.Append(zeros, element_size));
    }
  }
  RETURN_IF_ERROR(output.AppendPadding(padded - bytes));
  return output.Finish();
}

}  // namespace

ArrowSerializer::ArrowSerializer(IdColumnMode id_column_mode)
    : id_column_mode_(id_column_mode) {}

ArrowSerializer::~ArrowSerializer() = default;

void ArrowSerializer::Reset() {
  prepared_dataframe_ = nullptr;
  prepared_string_pool_ = nullptr;
  prepared_columns_.clear();
  dictionaries_.clear();
  header_.clear();
  batch_header_.clear();
  trailer_.clear();
}

// Collects the distinct StringPool ids of a column in first-appearance order
// and lays out the dictionary batch which will carry their bytes.
base::Status ArrowSerializer::PlanDictionary(uint32_t rows,
                                             const Column& column,
                                             const StringPool& pool,
                                             const BitVector* validity,
                                             bool sparse) {
  constexpr uint32_t kMaxData =
      static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
  constexpr size_t kMaxValues =
      static_cast<size_t>(std::numeric_limits<DictionaryIndex>::max());

  DictionaryPlan dictionary;
  const auto* ids = column.storage.unchecked_data<String>();
  StorageIndexMapper mapper(validity, sparse);
  for (uint32_t row = 0; row < rows; ++row) {
    if (!mapper.HasValue(row)) {
      continue;
    }
    StringPool::Id id = ids[mapper.Take(row)];
    auto [index, inserted] = dictionary.index_by_id.Insert(
        id.raw_id(), static_cast<uint32_t>(dictionary.values.size()));
    if (!inserted) {
      continue;
    }
    if (dictionary.values.size() == kMaxValues) {
      return base::ErrStatus("String column has too many distinct values");
    }
    size_t size = pool.Get(id).size();
    if (size > kMaxData - dictionary.data_length) {
      return base::ErrStatus("String column is too large for Arrow Utf8");
    }
    dictionary.data_length += static_cast<uint32_t>(size);
    dictionary.values.push_back(id);
  }

  auto values = static_cast<uint32_t>(dictionary.values.size());
  uint64_t cursor = 0;
  dictionary.buffers.push_back({0, 0});
  ASSIGN_OR_RETURN(auto offsets_buffer,
                   AddBuffer(Utf8OffsetBufferSize(values), &cursor));
  dictionary.buffers.push_back(offsets_buffer);
  ASSIGN_OR_RETURN(auto data_buffer,
                   AddBuffer(dictionary.data_length, &cursor));
  dictionary.buffers.push_back(data_buffer);
  dictionary.body_size = cursor;
  dictionaries_.push_back(std::move(dictionary));
  return base::OkStatus();
}

// Phase 1: derive the exact record-batch body layout from the dataframe. Arrow
// metadata stores every buffer offset before the body, so this pass also
// measures variable-width strings.
base::Status ArrowSerializer::PlanBody(const Dataframe& dataframe,
                                       const StringPool& pool,
                                       BodyPlan* plan) {
  plan->rows = dataframe.row_count();
  uint64_t body_cursor = 0;
  for (uint32_t i = 0; i < dataframe.column_count(); ++i) {
    const auto& column = *dataframe.columns_[i];
    StorageType type = column.storage.type();
    if (type.Is<core::Id>() && id_column_mode_ == IdColumnMode::kOmit) {
      continue;
    }
    PERFETTO_CHECK(type.Is<core::Id>() || type.Is<core::String>() ||
                   type.Is<core::Uint32>() || type.Is<core::Int32>() ||
                   type.Is<core::Int64>() || type.Is<core::Double>());

    Nullability nullability = column.null_storage.nullability();
    bool nullable = IsNullable(nullability);
    bool sparse = IsSparse(nullability);
    PreparedColumn prepared{i, dataframe.column_names_[i], nullable, type, 0};
    int64_t null_count = 0;
    if (nullable) {
      const BitVector& validity = column.null_storage.GetNullBitVector();
      null_count = CountNulls(plan->rows, validity);
      ASSIGN_OR_RETURN(auto validity_buffer,
                       AddBuffer(ValidityBufferSize(plan->rows), &body_cursor));
      plan->buffers.push_back(validity_buffer);
    } else {
      plan->buffers.push_back({static_cast<int64_t>(body_cursor), 0});
    }

    if (type.Is<core::String>()) {
      const BitVector* validity =
          nullable ? &column.null_storage.GetNullBitVector() : nullptr;
      prepared.dictionary = static_cast<uint32_t>(dictionaries_.size());
      RETURN_IF_ERROR(
          PlanDictionary(plan->rows, column, pool, validity, sparse));
      ASSIGN_OR_RETURN(
          auto index_buffer,
          AddBuffer(static_cast<uint64_t>(plan->rows) * sizeof(DictionaryIndex),
                    &body_cursor));
      plan->buffers.push_back(index_buffer);
    } else {
      ASSIGN_OR_RETURN(
          auto data_buffer,
          AddBuffer(static_cast<uint64_t>(plan->rows) * NumericSize(type),
                    &body_cursor));
      plan->buffers.push_back(data_buffer);
    }
    plan->nodes.push_back({plan->rows, null_count});
    prepared_columns_.push_back(std::move(prepared));
  }
  plan->size = body_cursor;
  return base::OkStatus();
}

// Phase 2: encode the schema, record-batch metadata, and footer around the body
// layout computed by PlanBody(). Arrow repeats the schema in the first message
// and in the footer.
base::Status ArrowSerializer::BuildFileFraming(const BodyPlan& plan) {
  auto build_schema = [this](W& w) {
    std::vector<W::Offset> fields;
    fields.reserve(prepared_columns_.size());
    for (const auto& column : prepared_columns_) {
      fields.push_back(BuildField(w, column.name, column.storage_type,
                                  column.nullable,
                                  static_cast<int64_t>(column.dictionary)));
    }
    auto field_vector =
        w.WriteVecOffsets(fields.data(), static_cast<uint32_t>(fields.size()));
    w.StartTable();
    w.FieldI16(schema_field::kEndianness, kEndiannessLittle);
    w.FieldOffset(schema_field::kFields, field_vector);
    return w.EndTable();
  };
  W schema_writer;
  std::vector<uint8_t> schema = FinishFlatbuffer(
      &schema_writer, BuildMessage(schema_writer, kHeaderSchema,
                                   build_schema(schema_writer), int64_t{}));
  W batch_writer;
  auto batch =
      BuildRecordBatch(batch_writer, plan.rows, plan.nodes, plan.buffers);
  std::vector<uint8_t> batch_metadata = FinishFlatbuffer(
      &batch_writer, BuildMessage(batch_writer, kHeaderRecordBatch, batch,
                                  static_cast<int64_t>(plan.size)));
  auto schema_size =
      static_cast<uint32_t>(PaddedSchemaMetadataSize(schema.size()));
  auto metadata_size =
      static_cast<uint32_t>(PaddedMetadataSize(batch_metadata.size()));
  uint32_t maximum_metadata_size = static_cast<uint32_t>(
      std::numeric_limits<int32_t>::max() - kMessagePrefixSize);
  if (schema_size > maximum_metadata_size ||
      metadata_size > maximum_metadata_size) {
    return base::ErrStatus("Arrow metadata is too large");
  }

  Append(&header_, kPaddedMagic, sizeof(kPaddedMagic));
  AppendMessage(&header_, schema, schema_size);

  uint64_t cursor = header_.size();
  std::vector<Block> dictionary_blocks;
  dictionary_blocks.reserve(dictionaries_.size());
  for (uint32_t i = 0; i < dictionaries_.size(); ++i) {
    DictionaryPlan& dictionary = dictionaries_[i];
    W writer;
    auto values = static_cast<uint32_t>(dictionary.values.size());
    std::vector<uint8_t> dictionary_metadata = FinishFlatbuffer(
        &writer, BuildMessage(writer, kHeaderDictionaryBatch,
                              BuildDictionaryBatch(writer, i, values,
                                                   dictionary.buffers),
                              static_cast<int64_t>(dictionary.body_size)));
    auto size =
        static_cast<uint32_t>(PaddedMetadataSize(dictionary_metadata.size()));
    if (size > maximum_metadata_size) {
      return base::ErrStatus("Arrow metadata is too large");
    }
    AppendMessage(&dictionary.message, dictionary_metadata, size);
    dictionary_blocks.push_back(
        Block{static_cast<int64_t>(cursor),
              static_cast<int32_t>(kMessagePrefixSize + size),
              {},
              static_cast<int64_t>(dictionary.body_size)});
    cursor += dictionary.message.size() + dictionary.body_size;
  }

  W footer_writer;
  Block block{static_cast<int64_t>(cursor),
              static_cast<int32_t>(kMessagePrefixSize + metadata_size),
              {},
              static_cast<int64_t>(plan.size)};
  std::vector<uint8_t> footer = FinishFlatbuffer(
      &footer_writer, BuildFooter(footer_writer, build_schema(footer_writer),
                                  dictionary_blocks, block));

  AppendMessage(&batch_header_, batch_metadata, metadata_size);
  // Readers which walk the message stream instead of the footer rely on the
  // end-of-stream marker to stop before the footer.
  AppendU32(&trailer_, kContinuation);
  AppendU32(&trailer_, 0);
  Append(&trailer_, footer.data(), footer.size());
  AppendU32(&trailer_, static_cast<uint32_t>(footer.size()));
  Append(&trailer_, kMagic, sizeof(kMagic));

  return base::OkStatus();
}

base::StatusOr<size_t> ArrowSerializer::Prepare(const Dataframe& dataframe,
                                                const StringPool& pool) {
  Reset();
  if (!dataframe.finalized()) {
    return base::ErrStatus(
        "Arrow serialization requires a finalized dataframe");
  }
  if (dataframe.string_pool_ != &pool) {
    return base::ErrStatus("String pool does not belong to dataframe");
  }

  BodyPlan plan;
  RETURN_IF_ERROR(PlanBody(dataframe, pool, &plan));
  RETURN_IF_ERROR(BuildFileFraming(plan));

  // Phase 3: validate the final platform-sized result and record the objects
  // whose identity and mutation count Write() must verify.
  constexpr uint64_t kMaxOutputSize = std::numeric_limits<size_t>::max();
  uint64_t total = header_.size() + batch_header_.size() + trailer_.size();
  for (const DictionaryPlan& dictionary : dictionaries_) {
    uint64_t dictionary_size = dictionary.message.size() + dictionary.body_size;
    if (dictionary_size > kMaxOutputSize - total) {
      return base::ErrStatus("Dataframe is too large for an Arrow file");
    }
    total += dictionary_size;
  }
  if (plan.size > kMaxOutputSize - total) {
    return base::ErrStatus("Dataframe is too large for an Arrow file");
  }
  total += plan.size;
  prepared_dataframe_ = &dataframe;
  prepared_string_pool_ = &pool;
  prepared_mutations_ = dataframe.mutations();
  return static_cast<size_t>(total);
}

base::Status ArrowSerializer::WriteDictionary(const DictionaryPlan& dictionary,
                                              const StringPool& pool,
                                              const WriteFn& write) {
  BufferedOutput output(&scratch_, write);
  RETURN_IF_ERROR(WriteUtf8OffsetBuffer(dictionary.values, pool,
                                        dictionary.data_length, &output));
  for (StringPool::Id id : dictionary.values) {
    NullTermStringView value = pool.Get(id);
    RETURN_IF_ERROR(output.Append(value.data(), value.size()));
  }
  RETURN_IF_ERROR(output.AppendPadding(static_cast<size_t>(
      AlignToArrow(dictionary.data_length) - dictionary.data_length)));
  return output.Finish();
}

base::Status ArrowSerializer::WriteDictionaryIndices(
    uint32_t rows,
    const Column& column,
    bool sparse,
    const BitVector* validity,
    const DictionaryPlan& dictionary,
    const WriteFn& write) {
  const auto* ids = column.storage.unchecked_data<String>();
  BufferedOutput output(&scratch_, write);
  StorageIndexMapper mapper(validity, sparse);
  for (uint32_t row = 0; row < rows; ++row) {
    DictionaryIndex index = 0;
    if (mapper.HasValue(row)) {
      const uint32_t* found =
          dictionary.index_by_id.Find(ids[mapper.Take(row)].raw_id());
      PERFETTO_CHECK(found);
      index = static_cast<DictionaryIndex>(*found);
    }
    RETURN_IF_ERROR(output.Append(&index, sizeof(index)));
  }
  size_t bytes = static_cast<size_t>(rows) * sizeof(DictionaryIndex);
  RETURN_IF_ERROR(
      output.AppendPadding(static_cast<size_t>(AlignToArrow(bytes)) - bytes));
  return output.Finish();
}

// Phase 4: materialize the buffers planned by PlanBody(). Each logical column
// is encoded as validity followed by one fixed-width buffer holding either the
// numeric values or the dictionary indices.
base::Status ArrowSerializer::WriteBody(const Dataframe& dataframe,
                                        const WriteFn& write) {
  uint32_t rows = dataframe.row_count();
  for (const auto& prepared : prepared_columns_) {
    const auto& column = *dataframe.columns_[prepared.dataframe_column];
    Nullability nullability = column.null_storage.nullability();
    bool sparse = IsSparse(nullability);
    const BitVector* validity = nullptr;
    if (prepared.nullable) {
      validity = &column.null_storage.GetNullBitVector();
      RETURN_IF_ERROR(WriteValidityBuffer(rows, *validity, &scratch_, write));
    }
    if (prepared.storage_type.Is<core::Id>()) {
      RETURN_IF_ERROR(WriteIdBuffer(rows, &scratch_, write));
    } else if (prepared.storage_type.Is<core::String>()) {
      RETURN_IF_ERROR(WriteDictionaryIndices(rows, column, sparse, validity,
                                             dictionaries_[prepared.dictionary],
                                             write));
    } else {
      RETURN_IF_ERROR(
          WriteNumericBuffer(rows, column, sparse, validity, &scratch_, write));
    }
  }
  return base::OkStatus();
}

base::Status ArrowSerializer::Write(const Dataframe& dataframe,
                                    const StringPool& pool,
                                    const WriteFn& write) {
  if (!prepared_dataframe_ || prepared_dataframe_ != &dataframe ||
      !dataframe.finalized() || prepared_string_pool_ != &pool ||
      dataframe.string_pool_ != &pool ||
      prepared_mutations_ != dataframe.mutations()) {
    return base::ErrStatus("Dataframe changed after Arrow Prepare");
  }
  RETURN_IF_ERROR(Emit(write, header_.data(), header_.size()));
  for (const DictionaryPlan& dictionary : dictionaries_) {
    RETURN_IF_ERROR(
        Emit(write, dictionary.message.data(), dictionary.message.size()));
    RETURN_IF_ERROR(WriteDictionary(dictionary, pool, write));
  }
  RETURN_IF_ERROR(Emit(write, batch_header_.data(), batch_header_.size()));
  RETURN_IF_ERROR(WriteBody(dataframe, write));
  return Emit(write, trailer_.data(), trailer_.size());
}

}  // namespace perfetto::trace_processor::core::dataframe
