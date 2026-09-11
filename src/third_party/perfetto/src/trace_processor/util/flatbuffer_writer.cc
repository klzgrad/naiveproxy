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

#include "src/trace_processor/util/flatbuffer_writer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"

namespace perfetto::trace_processor::util {

FlatBufferWriter::FlatBufferWriter(uint32_t initial_capacity)
    : buf_(initial_capacity, 0), head_(initial_capacity) {}

void FlatBufferWriter::GrowIfNeeded(uint32_t bytes) {
  if (head_ >= bytes) {
    return;
  }
  uint32_t old_size = static_cast<uint32_t>(buf_.size());
  uint32_t new_size = std::max(old_size * 2, old_size + bytes);
  buf_.resize(new_size, 0);
  // Shift existing data to the end of the new buffer.
  uint32_t data_len = old_size - head_;
  uint32_t new_head = new_size - data_len;
  memmove(buf_.data() + new_head, buf_.data() + head_, data_len);
  memset(buf_.data() + head_, 0, new_head - head_);
  head_ = new_head;
}

void FlatBufferWriter::Align(uint32_t alignment) {
  min_align_ = std::max(min_align_, alignment);
  uint32_t pad = (alignment - (size() % alignment)) % alignment;
  GrowIfNeeded(pad);
  for (uint32_t i = 0; i < pad; i++) {
    buf_[--head_] = 0;
  }
}

void FlatBufferWriter::PrependBytes(const void* src, uint32_t len) {
  if (len == 0) {
    return;
  }
  GrowIfNeeded(len);
  head_ -= len;
  memcpy(&buf_[head_], src, len);
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

FlatBufferWriter::Offset FlatBufferWriter::WriteString(std::string_view s) {
  // Final buffer layout: [u32 len][chars...][null][pad]. The padding must be
  // sized so that the LENGTH PREFIX (not the string tail) ends up 4-byte
  // aligned once the terminator and characters have been prepended.
  auto n = static_cast<uint32_t>(s.size());
  uint32_t pad = (4 - ((size() + n + 1) % 4)) % 4;
  GrowIfNeeded(pad);
  for (uint32_t i = 0; i < pad; i++) {
    buf_[--head_] = 0;
  }
  Prepend<uint8_t>(0);
  PrependBytes(s.data(), n);
  Prepend<uint32_t>(n);
  return Offset{size()};
}

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------

FlatBufferWriter::Offset FlatBufferWriter::WriteVecOffsets(
    const Offset* offsets,
    uint32_t count) {
  // Elements are written back-to-front (last element first).
  for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; i--) {
    Align(sizeof(uint32_t));
    // Relative offset: from the slot position to the target.
    uint32_t stored = size() + sizeof(uint32_t) - offsets[i].o;
    Prepend(stored);
  }
  Prepend<uint32_t>(count);
  return Offset{size()};
}

FlatBufferWriter::Offset FlatBufferWriter::WriteVecStruct(const void* data,
                                                          uint32_t elem_size,
                                                          uint32_t count,
                                                          uint32_t elem_align) {
  Align(elem_align);
  PrependBytes(data, elem_size * count);
  Prepend<uint32_t>(count);
  return Offset{size()};
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

void FlatBufferWriter::StartTable() {
  table_body_start_ = size();
  fields_.clear();
}

void FlatBufferWriter::FieldBool(uint32_t index, bool val) {
  Align(sizeof(uint8_t));
  Prepend<uint8_t>(val ? 1 : 0);
  fields_.push_back({index, size()});
}

void FlatBufferWriter::FieldU8(uint32_t index, uint8_t val) {
  Align(sizeof(uint8_t));
  Prepend(val);
  fields_.push_back({index, size()});
}

void FlatBufferWriter::FieldI16(uint32_t index, int16_t val) {
  Align(sizeof(int16_t));
  Prepend(val);
  fields_.push_back({index, size()});
}

void FlatBufferWriter::FieldI32(uint32_t index, int32_t val) {
  Align(sizeof(int32_t));
  Prepend(val);
  fields_.push_back({index, size()});
}

void FlatBufferWriter::FieldI64(uint32_t index, int64_t val) {
  Align(sizeof(int64_t));
  Prepend(val);
  fields_.push_back({index, size()});
}

void FlatBufferWriter::FieldOffset(uint32_t index, Offset off) {
  Align(sizeof(uint32_t));
  // Relative offset from the slot position to the target.
  uint32_t stored = size() + sizeof(uint32_t) - off.o;
  Prepend(stored);
  fields_.push_back({index, size()});
}

FlatBufferWriter::Offset FlatBufferWriter::EndTable() {
  // Prepend the soffset placeholder (patched below once the vtable position
  // is known). Its position must be tracked as an offset from the end of the
  // buffer: prepending the vtable below may grow the buffer, which shifts
  // all data and invalidates absolute indices.
  Align(sizeof(int32_t));
  Prepend<int32_t>(0);
  uint32_t table_off = size();

  uint32_t max_index = 0;
  for (const auto& f : fields_) {
    max_index = std::max(max_index, f.index);
  }
  uint32_t num_slots = fields_.empty() ? 0 : max_index + 1;
  uint32_t vtable_size = 4 + num_slots * 2;
  uint32_t table_size = table_off - table_body_start_;

  // Every vtable entry below is bounded by table_size, so this single check
  // covers all of them as well as the table_size entry itself.
  if (PERFETTO_UNLIKELY(table_size > std::numeric_limits<uint16_t>::max())) {
    PERFETTO_ELOG(
        "FlatBufferWriter: table size %u overflows the 16-bit vtable "
        "encoding; the resulting buffer will be corrupt",
        table_size);
  }

  // Each vtable entry is the byte offset from the table start (soffset
  // position) to the field data. 0 means "not present".
  std::vector<uint16_t> vt_entries(num_slots, 0);
  for (const auto& f : fields_) {
    vt_entries[f.index] = static_cast<uint16_t>(table_off - f.off_from_end);
  }

  // Prepend the vtable back-to-front: entries (last first), table_size,
  // vtable_size.
  Align(sizeof(uint16_t));
  for (int32_t i = static_cast<int32_t>(num_slots) - 1; i >= 0; i--) {
    Prepend(vt_entries[static_cast<uint32_t>(i)]);
  }
  Prepend(static_cast<uint16_t>(table_size));
  Prepend(static_cast<uint16_t>(vtable_size));
  uint32_t vtable_off = size();

  // Patch soffset: signed offset from the table to its vtable, i.e. the
  // reader computes vtable = table - soffset.
  auto soff = static_cast<int32_t>(vtable_off - table_off);
  memcpy(&buf_[static_cast<uint32_t>(buf_.size()) - table_off], &soff,
         sizeof(int32_t));

  return Offset{table_off};
}

// ---------------------------------------------------------------------------
// Finalize
// ---------------------------------------------------------------------------

void FlatBufferWriter::Finish(Offset root) {
  // Pad so that the finished buffer's size is a multiple of the largest
  // alignment used. All element positions are aligned relative to the
  // buffer end; an aligned total size makes them aligned relative to the
  // buffer start as well.
  uint32_t total = size() + sizeof(uint32_t);
  uint32_t pad = (min_align_ - (total % min_align_)) % min_align_;
  GrowIfNeeded(pad);
  for (uint32_t i = 0; i < pad; i++) {
    buf_[--head_] = 0;
  }
  uint32_t stored = size() + sizeof(uint32_t) - root.o;
  Prepend(stored);
}

std::vector<uint8_t> FlatBufferWriter::Release() {
  std::vector<uint8_t> result(buf_.begin() + head_, buf_.end());
  buf_.clear();
  head_ = 0;
  return result;
}

}  // namespace perfetto::trace_processor::util
