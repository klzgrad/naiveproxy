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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_INTERNAL_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "src/trace_processor/core/common/null_types.h"
#include "src/trace_processor/core/common/storage_types.h"

namespace perfetto::trace_processor::core::dataframe::arrow_internal {

static_assert(PERFETTO_IS_LITTLE_ENDIAN());

// Constants and packed structs below mirror the official Arrow flatbuffer
// schema. Keeping them together makes the serializer and deserializer use the
// same wire definitions without depending on the full Arrow library.
constexpr uint8_t kMagic[] = {'A', 'R', 'R', 'O', 'W', '1'};
constexpr uint8_t kPaddedMagic[] = {'A', 'R', 'R', 'O', 'W', '1', 0, 0};
constexpr uint32_t kContinuation = 0xFFFFFFFF;
constexpr int16_t kMetadataV5 = 4;
constexpr uint8_t kHeaderSchema = 1;
constexpr uint8_t kHeaderDictionaryBatch = 2;
constexpr uint8_t kHeaderRecordBatch = 3;
constexpr uint8_t kTypeInt = 2;
constexpr uint8_t kTypeFloatingPoint = 3;
constexpr uint8_t kTypeUtf8 = 5;
constexpr int16_t kPrecisionDouble = 2;
constexpr int16_t kEndiannessLittle = 0;

constexpr uint32_t kArrowAlignment = 64;
constexpr uint32_t kBitsPerByte = 8;
constexpr uint32_t kMessagePrefixSize = 8;
constexpr uint32_t kEndOfStreamSize = kMessagePrefixSize;
constexpr uint32_t kFooterSizeFieldSize = sizeof(uint32_t);
constexpr uint32_t kFileTrailerSize = kFooterSizeFieldSize + sizeof(kMagic);
constexpr uint32_t kMinimumFileSize =
    sizeof(kPaddedMagic) + kFooterSizeFieldSize + sizeof(kMagic);
constexpr uint32_t kFixedWidthBufferCount = 2;  // Validity + values.
constexpr uint32_t kUtf8BufferCount = 3;        // Validity + offsets + bytes.
constexpr uint32_t kRecordBatchCount = 1;
constexpr int64_t kMissingSignedValue = -1;

// String columns are dictionary encoded: the record batch holds int32 indices
// and the values live in a dictionary batch, mirroring how a dataframe stores
// StringPool ids.
constexpr int32_t kDictionaryIndexBits = 32;
using DictionaryIndex = int32_t;

// Field indices from the official Arrow flatbuffer schema.
namespace int_field {
constexpr uint32_t kBitWidth = 0;
constexpr uint32_t kIsSigned = 1;
}  // namespace int_field
namespace floating_point_field {
constexpr uint32_t kPrecision = 0;
}
namespace field_field {
constexpr uint32_t kName = 0;
constexpr uint32_t kNullable = 1;
constexpr uint32_t kTypeType = 2;
constexpr uint32_t kType = 3;
constexpr uint32_t kDictionary = 4;
}  // namespace field_field
namespace dictionary_encoding_field {
constexpr uint32_t kId = 0;
constexpr uint32_t kIndexType = 1;
constexpr uint32_t kIsOrdered = 2;
}  // namespace dictionary_encoding_field
namespace dictionary_batch_field {
constexpr uint32_t kId = 0;
constexpr uint32_t kData = 1;
constexpr uint32_t kIsDelta = 2;
}  // namespace dictionary_batch_field
namespace schema_field {
constexpr uint32_t kEndianness = 0;
constexpr uint32_t kFields = 1;
}  // namespace schema_field
namespace message_field {
constexpr uint32_t kVersion = 0;
constexpr uint32_t kHeaderType = 1;
constexpr uint32_t kHeader = 2;
constexpr uint32_t kBodyLength = 3;
}  // namespace message_field
namespace record_batch_field {
constexpr uint32_t kLength = 0;
constexpr uint32_t kNodes = 1;
constexpr uint32_t kBuffers = 2;
}  // namespace record_batch_field
namespace footer_field {
constexpr uint32_t kVersion = 0;
constexpr uint32_t kSchema = 1;
constexpr uint32_t kDictionaries = 2;
constexpr uint32_t kRecordBatches = 3;
}  // namespace footer_field

struct MessagePrefix {
  uint32_t continuation;
  int32_t metadata_size;
};
static_assert(sizeof(MessagePrefix) == kMessagePrefixSize);

struct FieldNode {
  int64_t length;
  int64_t null_count;
};
static_assert(sizeof(FieldNode) == 2 * sizeof(int64_t));

struct ArrowBuffer {
  int64_t offset;
  int64_t length;
};
static_assert(sizeof(ArrowBuffer) == 2 * sizeof(int64_t));

struct Block {
  int64_t offset;
  int32_t metadata_length;
  int32_t padding;
  int64_t body_length;
};
static_assert(sizeof(Block) == 2 * sizeof(int64_t) + 2 * sizeof(int32_t));

inline base::Status InvalidFile() {
  return base::ErrStatus("Invalid dataframe Arrow file");
}

inline uint64_t AlignToArrow(uint64_t value) {
  constexpr uint64_t kMask = kArrowAlignment - 1;
  return (value + kMask) & ~kMask;
}

// A message body starts immediately after the message prefix and the padded
// metadata, so the metadata padding is what decides whether the body lands on
// an aligned file offset.
inline uint64_t PaddedMetadataSize(uint64_t metadata_size) {
  return AlignToArrow(metadata_size + kMessagePrefixSize) - kMessagePrefixSize;
}

// The schema message is the first message in the file, so its padding also has
// to absorb the leading magic to keep every later message aligned.
inline uint64_t PaddedSchemaMetadataSize(uint64_t metadata_size) {
  constexpr uint64_t kLeading = sizeof(kPaddedMagic) + kMessagePrefixSize;
  return AlignToArrow(metadata_size + kLeading) - kLeading;
}

inline uint64_t ValidityBufferSize(uint32_t rows) {
  return (static_cast<uint64_t>(rows) + kBitsPerByte - 1) / kBitsPerByte;
}

inline uint64_t Utf8OffsetBufferSize(uint32_t values) {
  return (static_cast<uint64_t>(values) + 1) * sizeof(int32_t);
}

inline size_t BitmapByteIndex(uint32_t row) {
  return row / kBitsPerByte;
}

inline uint8_t BitmapMask(uint32_t row) {
  return static_cast<uint8_t>(1u << (row % kBitsPerByte));
}

inline uint32_t NumericSize(StorageType type) {
  if (type.Is<core::Int64>() || type.Is<core::Double>()) {
    return sizeof(int64_t);
  }
  PERFETTO_CHECK(type.Is<core::Id>() || type.Is<core::Uint32>() ||
                 type.Is<core::Int32>());
  return sizeof(int32_t);
}

inline bool IsNullable(Nullability type) {
  return !type.Is<core::NonNull>();
}

inline bool IsSparse(Nullability type) {
  return IsNullable(type) && !type.Is<core::DenseNull>();
}

template <typename T>
T Load(const uint8_t* data, uint32_t index) {
  T value;
  memcpy(&value, data + static_cast<size_t>(index) * sizeof(T), sizeof(T));
  return value;
}

}  // namespace perfetto::trace_processor::core::dataframe::arrow_internal

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_INTERNAL_H_
