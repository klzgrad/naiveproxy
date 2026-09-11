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

#ifndef SRC_TRACE_PROCESSOR_UTIL_FLATBUFFER_WRITER_H_
#define SRC_TRACE_PROCESSOR_UTIL_FLATBUFFER_WRITER_H_

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace perfetto::trace_processor::util {

// Minimal FlatBuffer writer. Builds a valid flatbuffer back-to-front (the
// standard approach) so all offsets are naturally forward-pointing.
//
// Exists so trace processor can emit small flatbuffer-encoded metadata (e.g.
// Arrow IPC framing) without depending on the flatbuffers library.
//
// Usage:
//   FlatBufferWriter w;
//   auto name = w.WriteString("hello");
//   w.StartTable();
//   w.FieldOffset(0, name);
//   w.FieldI32(1, 42);
//   auto root = w.EndTable();
//   w.Finish(root);
//   auto buf = w.Release();
//
// Buffers produced by this writer can be parsed by
// FlatBufferReader::GetRoot() (see flatbuffer_reader.h).
class FlatBufferWriter {
 public:
  // Opaque handle to a previously written object's position, expressed as
  // "bytes from the end of the buffer".
  struct Offset {
    uint32_t o = 0;
  };

  explicit FlatBufferWriter(uint32_t initial_capacity = 256);

  // Writes a length-prefixed UTF-8 string. Returns an offset handle.
  Offset WriteString(std::string_view s);

  // Writes a vector of offset elements (for vectors of tables or strings).
  Offset WriteVecOffsets(const Offset* offsets, uint32_t count);

  // Writes a vector of inline structs. |data| points to |count| packed
  // structs of |elem_size| bytes each, aligned to |elem_align|.
  Offset WriteVecStruct(const void* data,
                        uint32_t elem_size,
                        uint32_t count,
                        uint32_t elem_align);

  // Begins building a table. All Field* calls between StartTable/EndTable add
  // fields to this table. Tables cannot be nested: finish the child table
  // first, then reference it via FieldOffset.
  void StartTable();

  // Adds scalar fields to the table under construction.
  void FieldBool(uint32_t index, bool val);
  void FieldU8(uint32_t index, uint8_t val);
  void FieldI16(uint32_t index, int16_t val);
  void FieldI32(uint32_t index, int32_t val);
  void FieldI64(uint32_t index, int64_t val);

  // Adds an offset field (string, sub-table, or vector).
  void FieldOffset(uint32_t index, Offset off);

  // Finishes the current table. Returns an offset handle.
  Offset EndTable();

  // Finalizes the buffer by writing the root table offset prefix.
  void Finish(Offset root);

  // Access the finished buffer.
  const uint8_t* data() const { return buf_.data() + head_; }
  uint32_t size() const { return static_cast<uint32_t>(buf_.size()) - head_; }

  // Takes ownership of the finished buffer as a tight vector.
  std::vector<uint8_t> Release();

 private:
  // Ensures at least |bytes| of space is available before head_.
  void GrowIfNeeded(uint32_t bytes);

  // Pads so that the current written size is a multiple of |alignment|.
  void Align(uint32_t alignment);

  // Prepends raw bytes (order preserved in the final buffer).
  void PrependBytes(const void* src, uint32_t len);

  // Prepends a single scalar value.
  template <typename T>
  void Prepend(T val) {
    GrowIfNeeded(sizeof(T));
    head_ -= sizeof(T);
    memcpy(&buf_[head_], &val, sizeof(T));
  }

  std::vector<uint8_t> buf_;
  uint32_t head_ = 0;
  // Largest alignment any element requested. Element positions are aligned
  // relative to the buffer end, so Finish() pads the total size to a
  // multiple of this to make them aligned relative to the start too (which
  // is what strict flatbuffers verifiers check).
  uint32_t min_align_ = 4;

  // State for the table currently being built.
  uint32_t table_body_start_ = 0;
  struct FieldLoc {
    uint32_t index;
    uint32_t off_from_end;
  };
  std::vector<FieldLoc> fields_;
};

}  // namespace perfetto::trace_processor::util

#endif  // SRC_TRACE_PROCESSOR_UTIL_FLATBUFFER_WRITER_H_
