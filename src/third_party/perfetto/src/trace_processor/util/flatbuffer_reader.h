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

#ifndef SRC_TRACE_PROCESSOR_UTIL_FLATBUFFER_READER_H_
#define SRC_TRACE_PROCESSOR_UTIL_FLATBUFFER_READER_H_

#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

#include "perfetto/base/compiler.h"

namespace perfetto::trace_processor::util {

// Minimal read-only FlatBuffer API, sufficient for parsing Arrow IPC metadata
// and similar small schemas without pulling in the flatbuffers library.
//
// Every access is bounds-checked against the buffer passed at construction:
// this reader is designed to be safe on untrusted input. Absent or malformed
// fields uniformly read back as defaults (empty views, invalid readers),
// never out-of-bounds accesses.
//
// FlatBuffer wire format recap:
//   - A Table starts with an int32 "soffset" pointing backwards to its vtable.
//   - VTable layout: [u16 vtable_size, u16 table_size, u16 field0_off, ...]
//   - Field offsets are relative to the table start; 0 means "not present".
//   - Strings: u32 length followed by UTF-8 bytes + null terminator.
//   - Vectors: u32 element count followed by the elements.
//   - Offsets (to strings, tables, vectors) are u32 relative to the offset's
//     own position: target = &offset + *offset.
//   - Structs are stored inline (no vtable indirection).
//   - The root table offset is at the very start of the buffer.

// A typed view over a vector of inline scalars (or packed structs) in a
// flatbuffer. The whole extent [data, data + size * sizeof(T)) is validated
// at creation; out-of-range indices read back as a default-constructed T.
template <typename T>
class FlatBufferScalarVec {
 public:
  FlatBufferScalarVec() = default;
  FlatBufferScalarVec(const uint8_t* data, uint32_t size)
      : data_(data), size_(size) {}

  uint32_t size() const { return size_; }

  T operator[](uint32_t i) const {
    if (PERFETTO_UNLIKELY(i >= size_)) {
      return T{};
    }
    T v;
    memcpy(&v, data_ + static_cast<uint64_t>(i) * sizeof(T), sizeof(T));
    return v;
  }

 private:
  const uint8_t* data_ = nullptr;
  uint32_t size_ = 0;
};

class FlatBufferReader;

// A view over a vector of table offsets in a flatbuffer.
class FlatBufferTableVec {
 public:
  FlatBufferTableVec() = default;
  FlatBufferTableVec(const uint8_t* buf,
                     uint32_t buf_size,
                     const uint8_t* base,
                     uint32_t count)
      : buf_(buf), buf_size_(buf_size), base_(base), count_(count) {}

  uint32_t size() const { return count_; }
  // Returns an invalid reader (operator bool == false) if the element's
  // offset is out of bounds.
  FlatBufferReader operator[](uint32_t i) const;

 private:
  const uint8_t* buf_ = nullptr;
  uint32_t buf_size_ = 0;
  const uint8_t* base_ = nullptr;
  uint32_t count_ = 0;
};

// A view over a vector of string offsets in a flatbuffer.
class FlatBufferStringVec {
 public:
  FlatBufferStringVec() = default;
  FlatBufferStringVec(const uint8_t* buf,
                      uint32_t buf_size,
                      const uint8_t* base,
                      uint32_t count)
      : buf_(buf), buf_size_(buf_size), base_(base), count_(count) {}

  uint32_t size() const { return count_; }
  // Returns an empty view if the element's offset is out of bounds.
  std::string_view operator[](uint32_t i) const;

 private:
  const uint8_t* buf_ = nullptr;
  uint32_t buf_size_ = 0;
  const uint8_t* base_ = nullptr;
  uint32_t count_ = 0;
};

// Read-only handle to a single flatbuffer table. Zero-copy: just pointer
// arithmetic over the underlying buffer. A default-constructed reader
// represents a missing/absent table; all accessors return defaults.
class FlatBufferReader {
 public:
  FlatBufferReader() = default;
  FlatBufferReader(const uint8_t* buf, uint32_t buf_size, const uint8_t* table)
      : buf_(buf), buf_size_(buf_size), table_(table) {}

  // Returns a pointer to the raw field data at |field_index|, or nullptr if
  // the field is absent (vtable too short or offset is zero) or would point
  // outside the buffer. |field_size| bytes starting at the returned pointer
  // are guaranteed to be in bounds.
  const uint8_t* FieldRaw(uint32_t field_index, uint32_t field_size) const;

  // Reads a scalar field stored inline at the vtable offset.
  template <typename T>
  T Scalar(uint32_t field_index, T default_val = {}) const {
    const uint8_t* p = FieldRaw(field_index, sizeof(T));
    if (PERFETTO_UNLIKELY(!p)) {
      return default_val;
    }
    T v;
    memcpy(&v, p, sizeof(T));
    return v;
  }

  // Reads a string field (offset -> length-prefixed UTF-8). Empty view if
  // absent or malformed.
  std::string_view String(uint32_t field_index) const;

  // Reads a sub-table field (offset -> child table). Invalid reader if
  // absent or malformed.
  FlatBufferReader Table(uint32_t field_index) const;

  // Reads a vector of inline scalars or packed structs of type T. Empty view
  // if absent, malformed or extending past the end of the buffer.
  template <typename T>
  FlatBufferScalarVec<T> VecScalar(uint32_t field_index) const {
    uint32_t count = 0;
    const uint8_t* elems = VecRaw(field_index, sizeof(T), &count);
    if (PERFETTO_UNLIKELY(!elems)) {
      return {};
    }
    return {elems, count};
  }

  // Reads a vector of tables. Empty view if absent or malformed.
  FlatBufferTableVec VecTable(uint32_t field_index) const;

  // Reads a vector of strings. Empty view if absent or malformed.
  FlatBufferStringVec VecString(uint32_t field_index) const;

  // Returns the root table reader for a flatbuffer at |data| of |size|
  // bytes, or std::nullopt if the buffer is too small or the root offset is
  // out of bounds.
  static std::optional<FlatBufferReader> GetRoot(const uint8_t* data,
                                                 uint32_t size);

  explicit operator bool() const { return table_ != nullptr; }

 private:
  friend class FlatBufferTableVec;
  friend class FlatBufferStringVec;

  static uint16_t ReadU16(const uint8_t* p);
  static uint32_t ReadU32(const uint8_t* p);

  // Follows a relative u32 offset at |p|, requiring |target_size| bytes to be
  // available at the target. Returns nullptr if out of bounds.
  const uint8_t* FollowOffset(const uint8_t* p, uint32_t target_size) const;

  // Resolves an offset field to a vector and validates that all |*count|
  // elements of |elem_size| bytes are in bounds. Returns a pointer to the
  // first element, or nullptr on absence or bounds failure.
  const uint8_t* VecRaw(uint32_t field_index,
                        uint32_t elem_size,
                        uint32_t* count) const;

  const uint8_t* buf_ = nullptr;
  uint32_t buf_size_ = 0;
  const uint8_t* table_ = nullptr;
};

}  // namespace perfetto::trace_processor::util

#endif  // SRC_TRACE_PROCESSOR_UTIL_FLATBUFFER_READER_H_
