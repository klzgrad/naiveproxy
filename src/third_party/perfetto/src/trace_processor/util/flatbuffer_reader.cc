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

#include "src/trace_processor/util/flatbuffer_reader.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

#include "perfetto/base/compiler.h"

namespace perfetto::trace_processor::util {

// static
uint16_t FlatBufferReader::ReadU16(const uint8_t* p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

// static
uint32_t FlatBufferReader::ReadU32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

// All bounds checks below work in the "byte offset from buf_" domain using
// uint64_t/int64_t arithmetic. This keeps every comparison immune to 32-bit
// wraparound (offsets read from an untrusted buffer are full-range uint32_t)
// and never forms a pointer before its target offset has been checked
// against buf_size_.
const uint8_t* FlatBufferReader::FollowOffset(const uint8_t* p,
                                              uint32_t target_size) const {
  uint32_t rel = ReadU32(p);
  uint64_t p_offset = static_cast<uint64_t>(p - buf_);
  uint64_t target_offset = p_offset + rel;
  if (PERFETTO_UNLIKELY(target_offset > buf_size_)) {
    return nullptr;
  }
  uint64_t remaining = static_cast<uint64_t>(buf_size_) - target_offset;
  if (PERFETTO_UNLIKELY(target_size > remaining)) {
    return nullptr;
  }
  return buf_ + target_offset;
}

const uint8_t* FlatBufferReader::FieldRaw(uint32_t field_index,
                                          uint32_t field_size) const {
  if (PERFETTO_UNLIKELY(!table_)) {
    return nullptr;
  }
  // Invariant: every non-null table_ was validated (by GetRoot/Table/
  // FlatBufferTableVec) to have at least 4 bytes available for its soffset.
  int32_t soff;
  memcpy(&soff, table_, 4);
  uint64_t table_offset = static_cast<uint64_t>(table_ - buf_);
  int64_t voffset = static_cast<int64_t>(table_offset) - soff;
  if (PERFETTO_UNLIKELY(voffset < 0 ||
                        static_cast<uint64_t>(voffset) > buf_size_)) {
    return nullptr;
  }
  uint64_t vtable_offset = static_cast<uint64_t>(voffset);
  uint64_t remaining_at_vtable =
      static_cast<uint64_t>(buf_size_) - vtable_offset;
  if (PERFETTO_UNLIKELY(remaining_at_vtable < 4)) {
    return nullptr;
  }
  const uint8_t* vtable = buf_ + vtable_offset;
  uint16_t vtable_size = ReadU16(vtable);

  uint64_t slot_pos = 4 + static_cast<uint64_t>(field_index) * 2;
  if (PERFETTO_UNLIKELY(slot_pos + 2 > vtable_size)) {
    return nullptr;  // Field absent: beyond the vtable's declared extent.
  }
  if (PERFETTO_UNLIKELY(slot_pos + 2 > remaining_at_vtable)) {
    return nullptr;  // Malformed: vtable_size lies about the buffer size.
  }
  uint16_t field_off = ReadU16(vtable + slot_pos);
  if (PERFETTO_UNLIKELY(field_off == 0)) {
    return nullptr;  // Field absent.
  }

  uint64_t field_offset = table_offset + field_off;
  if (PERFETTO_UNLIKELY(field_offset > buf_size_)) {
    return nullptr;
  }
  uint64_t remaining_at_field = static_cast<uint64_t>(buf_size_) - field_offset;
  if (PERFETTO_UNLIKELY(field_size > remaining_at_field)) {
    return nullptr;
  }
  return buf_ + field_offset;
}

std::string_view FlatBufferReader::String(uint32_t field_index) const {
  const uint8_t* p = FieldRaw(field_index, 4);
  if (PERFETTO_UNLIKELY(!p)) {
    return {};
  }
  const uint8_t* s = FollowOffset(p, 4);
  if (PERFETTO_UNLIKELY(!s)) {
    return {};
  }
  uint32_t len = ReadU32(s);
  uint64_t data_offset = static_cast<uint64_t>(s - buf_) + 4;
  uint64_t remaining = static_cast<uint64_t>(buf_size_) - data_offset;
  if (PERFETTO_UNLIKELY(len > remaining)) {
    return {};
  }
  return {reinterpret_cast<const char*>(s + 4), len};
}

FlatBufferReader FlatBufferReader::Table(uint32_t field_index) const {
  const uint8_t* p = FieldRaw(field_index, 4);
  if (PERFETTO_UNLIKELY(!p)) {
    return {};
  }
  const uint8_t* t = FollowOffset(p, 4);
  if (PERFETTO_UNLIKELY(!t)) {
    return {};
  }
  return {buf_, buf_size_, t};
}

const uint8_t* FlatBufferReader::VecRaw(uint32_t field_index,
                                        uint32_t elem_size,
                                        uint32_t* count) const {
  const uint8_t* p = FieldRaw(field_index, 4);
  if (PERFETTO_UNLIKELY(!p)) {
    return nullptr;
  }
  const uint8_t* vec = FollowOffset(p, 4);
  if (PERFETTO_UNLIKELY(!vec)) {
    return nullptr;
  }
  uint32_t n = ReadU32(vec);
  uint64_t elems_offset = static_cast<uint64_t>(vec - buf_) + 4;
  uint64_t remaining = static_cast<uint64_t>(buf_size_) - elems_offset;
  uint64_t needed = static_cast<uint64_t>(n) * static_cast<uint64_t>(elem_size);
  if (PERFETTO_UNLIKELY(needed > remaining)) {
    return nullptr;
  }
  *count = n;
  return buf_ + elems_offset;
}

FlatBufferTableVec FlatBufferReader::VecTable(uint32_t field_index) const {
  uint32_t count = 0;
  const uint8_t* base = VecRaw(field_index, 4, &count);
  if (PERFETTO_UNLIKELY(!base)) {
    return {};
  }
  return {buf_, buf_size_, base, count};
}

FlatBufferStringVec FlatBufferReader::VecString(uint32_t field_index) const {
  uint32_t count = 0;
  const uint8_t* base = VecRaw(field_index, 4, &count);
  if (PERFETTO_UNLIKELY(!base)) {
    return {};
  }
  return {buf_, buf_size_, base, count};
}

// static
std::optional<FlatBufferReader> FlatBufferReader::GetRoot(const uint8_t* data,
                                                          uint32_t size) {
  if (PERFETTO_UNLIKELY(size < 4)) {
    return std::nullopt;
  }
  FlatBufferReader tmp(data, size, nullptr);
  const uint8_t* root = tmp.FollowOffset(data, 4);
  if (PERFETTO_UNLIKELY(!root)) {
    return std::nullopt;
  }
  return FlatBufferReader(data, size, root);
}

FlatBufferReader FlatBufferTableVec::operator[](uint32_t i) const {
  if (PERFETTO_UNLIKELY(i >= count_)) {
    return {};
  }
  const uint8_t* slot = base_ + static_cast<uint64_t>(i) * 4;
  FlatBufferReader tmp(buf_, buf_size_, nullptr);
  const uint8_t* target = tmp.FollowOffset(slot, 4);
  if (PERFETTO_UNLIKELY(!target)) {
    return {};
  }
  return {buf_, buf_size_, target};
}

std::string_view FlatBufferStringVec::operator[](uint32_t i) const {
  if (PERFETTO_UNLIKELY(i >= count_)) {
    return {};
  }
  const uint8_t* slot = base_ + static_cast<uint64_t>(i) * 4;
  FlatBufferReader tmp(buf_, buf_size_, nullptr);
  const uint8_t* s = tmp.FollowOffset(slot, 4);
  if (PERFETTO_UNLIKELY(!s)) {
    return {};
  }
  uint32_t len = FlatBufferReader::ReadU32(s);
  uint64_t data_offset = static_cast<uint64_t>(s - buf_) + 4;
  uint64_t remaining = static_cast<uint64_t>(buf_size_) - data_offset;
  if (PERFETTO_UNLIKELY(len > remaining)) {
    return {};
  }
  return {reinterpret_cast<const char*>(s + 4), len};
}

}  // namespace perfetto::trace_processor::util
