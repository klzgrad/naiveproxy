/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "perfetto/protozero/proto_decoder.h"

#include <string.h>

#include <cinttypes>
#include <limits>
#include <memory>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/public/compiler.h"

namespace protozero {

using namespace proto_utils;

#if !PERFETTO_IS_LITTLE_ENDIAN()
#error Unimplemented for big endian archs.
#endif

const Field TypedProtoDecoderBase::kInvalidField{};

namespace {

struct ParseFieldResult {
  enum ParseResult { kAbort, kSkip, kOk };
  ParseResult parse_res;
  const uint8_t* next;
  Field field;
};

// Parses one field and returns the field itself and a pointer to the next
// field to parse. If parsing fails, the returned |next| == |buffer|.
// Force-inlined so that, after inlining, the result struct is decomposed by
// the compiler and does not round-trip through memory.
PERFETTO_ALWAYS_INLINE ParseFieldResult
ParseOneField(const uint8_t* const buffer, const uint8_t* const end) {
  ParseFieldResult res{ParseFieldResult::kAbort, buffer, Field{}};

  const uint8_t* pos = buffer;

  // If we've already hit the end, just return an invalid field.
  if (PERFETTO_UNLIKELY(pos >= end))
    return res;

  uint64_t preamble = 0;
  if (PERFETTO_LIKELY(*pos < 0x80)) {  // Fastpath for fields with ID < 16.
    preamble = *(pos++);
  } else {
    const uint8_t* next = ParseVarIntFast(pos, end, &preamble);
    if (PERFETTO_UNLIKELY(!next))
      return res;
    pos = next;
  }

  uint32_t field_id =
      static_cast<uint32_t>(proto_utils::GetTagFieldId(preamble));
  if (field_id == 0 || pos >= end)
    return res;

  auto field_type =
      static_cast<uint8_t>(proto_utils::GetTagFieldType(preamble));
  const uint8_t* new_pos = pos;
  uint64_t int_value = 0;
  uint64_t size = 0;

  // If-chain ordered by observed frequency: wire types within a message are
  // near-constant, so these branches predict better than a switch jump.
  if (PERFETTO_LIKELY(field_type ==
                      static_cast<uint8_t>(ProtoWireType::kVarInt))) {
    new_pos = ParseVarIntFast(pos, end, &int_value);

    // new_pos being null means ParseVarIntFast could not fully parse the
    // number. This is because we are out of space in the buffer. Set the id
    // to zero and return but don't update the offset so a future read can
    // read this field.
    if (PERFETTO_UNLIKELY(!new_pos))
      return res;
  } else if (PERFETTO_LIKELY(
                 field_type ==
                 static_cast<uint8_t>(ProtoWireType::kLengthDelimited))) {
    uint64_t payload_length;
    new_pos = ParseVarIntFast(pos, end, &payload_length);
    if (PERFETTO_UNLIKELY(!new_pos))
      return res;

    // ParseVarIntFast guarantees that |new_pos| <= |end| when it succeeds;
    if (payload_length > static_cast<uint64_t>(end - new_pos))
      return res;

    const uintptr_t payload_start = reinterpret_cast<uintptr_t>(new_pos);
    int_value = payload_start;
    size = payload_length;
    new_pos += payload_length;
  } else if (field_type == static_cast<uint8_t>(ProtoWireType::kFixed64)) {
    new_pos = pos + sizeof(uint64_t);
    if (PERFETTO_UNLIKELY(new_pos > end))
      return res;
    memcpy(&int_value, pos, sizeof(uint64_t));
  } else if (field_type == static_cast<uint8_t>(ProtoWireType::kFixed32)) {
    new_pos = pos + sizeof(uint32_t);
    if (PERFETTO_UNLIKELY(new_pos > end))
      return res;
    memcpy(&int_value, pos, sizeof(uint32_t));
  } else {
    PERFETTO_DLOG("Invalid proto field type: %u", field_type);
    return res;
  }

  res.next = new_pos;

  if (PERFETTO_UNLIKELY(field_id > Field::kMaxId)) {
    PERFETTO_DLOG("Skipping field %" PRIu32 " because its id > %" PRIu32,
                  field_id, Field::kMaxId);
    res.parse_res = ParseFieldResult::kSkip;
    return res;
  }

  if (PERFETTO_UNLIKELY(size > proto_utils::kMaxMessageLength)) {
    PERFETTO_DLOG("Skipping field %" PRIu32 " because it's too big (%" PRIu64
                  " KB)",
                  field_id, size / 1024);
    res.parse_res = ParseFieldResult::kSkip;
    return res;
  }

  res.parse_res = ParseFieldResult::kOk;
  res.field.initialize(field_id, field_type, int_value,
                       static_cast<uint32_t>(size));
  return res;
}

// Grows the selective-decoding spill area once the inline slots are
// exhausted, moving the spilled fields to the heap. The append itself is
// open-coded in the decode loop; see the comment there.
PERFETTO_NO_INLINE void ExpandSpill(Field** spill,
                                    uint32_t* spill_capacity,
                                    uint32_t spill_size,
                                    std::unique_ptr<Field[]>* heap_spill) {
  // Double the capacity, with a floor so that an exhausted spill that somehow
  // starts from zero capacity (e.g. a moved-from state) doesn't keep doubling
  // zero. The floor matches SelectiveTypedProtoDecoder::kSpillStackCapacity.
  const uint32_t new_capacity = std::max(*spill_capacity * 2, 16u);
  PERFETTO_CHECK(new_capacity > spill_size);
  std::unique_ptr<Field[]> new_storage(new Field[new_capacity]);
  if (spill_size > 0)
    memcpy(&new_storage[0], *spill, sizeof(Field) * spill_size);
  *heap_spill = std::move(new_storage);
  *spill = &(*heap_spill)[0];
  *spill_capacity = new_capacity;
}

}  // namespace

Field ProtoDecoder::FindField(uint32_t field_id) {
  Field res{};
  auto old_position = read_ptr_;
  read_ptr_ = begin_;
  for (auto f = ReadField(); f.valid(); f = ReadField()) {
    if (f.id() == field_id) {
      res = f;
      break;
    }
  }
  read_ptr_ = old_position;
  return res;
}

Field ProtoDecoder::ReadField() {
  ParseFieldResult res;
  do {
    res = ParseOneField(read_ptr_, end_);
    read_ptr_ = res.next;
  } while (PERFETTO_UNLIKELY(res.parse_res == ParseFieldResult::kSkip));
  return res.field;
}

void TypedProtoDecoderBase::ParseAllFields() {
  ParseAllFieldsImpl<false>(nullptr, nullptr, nullptr, nullptr, nullptr);
}

void TypedProtoDecoderBase::ParseAllFieldsSelective(
    const uint64_t* dense_mask,
    Field** spill,
    uint32_t* spill_capacity,
    uint32_t* spill_size,
    std::unique_ptr<Field[]>* heap_spill) {
  PERFETTO_DCHECK(dense_mask && spill && spill_capacity && spill_size &&
                  heap_spill);
  ParseAllFieldsImpl<true>(dense_mask, spill, spill_capacity, spill_size,
                           heap_spill);
}

// Force-inlined into the two thin entry points above: the plain
// instantiation compiles to a loop with no trace of the mask support and
// the wrappers' arguments fold away entirely.
template <bool kSelective>
PERFETTO_ALWAYS_INLINE void TypedProtoDecoderBase::ParseAllFieldsImpl(
    const uint64_t* dense_mask,
    Field** spill,
    uint32_t* spill_capacity,
    uint32_t* spill_size,
    std::unique_ptr<Field[]>* heap_spill) {
  const uint8_t* cur = begin_;
  ParseFieldResult res;
  for (;;) {
    res = ParseOneField(cur, end_);
    PERFETTO_DCHECK(res.parse_res != ParseFieldResult::kOk || res.next != cur);
    cur = res.next;
    if (PERFETTO_UNLIKELY(res.parse_res == ParseFieldResult::kSkip))
      continue;
    if (PERFETTO_UNLIKELY(res.parse_res == ParseFieldResult::kAbort))
      break;

    PERFETTO_DCHECK(res.parse_res == ParseFieldResult::kOk);
    PERFETTO_DCHECK(res.field.valid());
    auto field_id = res.field.id();
    if constexpr (kSelective) {
      // Selective decoding: only allowlisted in-range ids go to the dense
      // storage; everything else (including ids beyond the in-tree range) is
      // appended, in wire order, to the spill area. |num_fields_| is capped
      // at the mask's extent by SelectiveTypedProtoDecoder, so this also
      // bounds the mask read.
      const bool dense =
          field_id < num_fields_ &&
          (dense_mask[field_id / 64] & (1ULL << (field_id % 64))) != 0;
      if (!dense) {
        // The spill append is open-coded from the field's scalar components:
        // a function taking the whole Field (by value or reference) makes
        // the compiler materialize the packed Field representation on every
        // loop iteration, including for fields that are not spilled.
        if (PERFETTO_UNLIKELY(*spill_size >= *spill_capacity))
          ExpandSpill(spill, spill_capacity, *spill_size, heap_spill);
        (*spill)[(*spill_size)++].initialize(
            field_id, static_cast<uint8_t>(res.field.type()),
            res.field.raw_int_value(), res.field.raw_size());
        continue;
      }
    } else if (PERFETTO_UNLIKELY(field_id >= num_fields_)) {
      // Fields with an id beyond the highest field id known in-tree at compile
      // time are out-of-tree "extension" fields (e.g. carved out of
      // TracePacket's `extensions 1000 to 1999` range). They are intentionally
      // not stored here because fields_ is indexed directly by field id, and
      // extension ids are sparse and potentially very high. Callers that need
      // them should decode selectively and consume them from unknown_fields().
      continue;
    }

    // There are two reasons why we might want to expand the heap capacity:
    // 1. We are writing a non-repeated field, which has an id >
    //    INITIAL_STACK_CAPACITY. In this case ExpandHeapStorage() ensures to
    //    allocate at least (num_fields_ + 1) slots.
    // 2. We are writing a repeated field but ran out of capacity.
    if (PERFETTO_UNLIKELY(field_id >= size_ || size_ >= capacity_))
      ExpandHeapStorage();

    PERFETTO_DCHECK(field_id < size_);
    Field* fld = &fields_[field_id];
    if (PERFETTO_LIKELY(!HasField(field_id))) {
      // This is the first time we see this field.
      SetField(field_id);
      *fld = std::move(res.field);
    } else {
      // Repeated field case.
      // In this case we need to:
      // 1. Append the last value of the field to end of the repeated field
      //    storage.
      // 2. Replace the default instance at offset |field_id| with the current
      //    value. This is because in case of repeated field a call to Get(X) is
      //    supposed to return the last value of X, not the first one.
      // This is so that the RepeatedFieldIterator will iterate in the right
      // order, see comments on RepeatedFieldIterator.
      if (num_fields_ > size_) {
        ExpandHeapStorage();
        fld = &fields_[field_id];
      }

      PERFETTO_DCHECK(size_ < capacity_);
      fields_[size_++] = *fld;
      *fld = std::move(res.field);
    }
  }
  read_ptr_ = res.next;
}

void TypedProtoDecoderBase::ExpandHeapStorage() {
  // When we expand the heap we must ensure that we have at very last capacity
  // to deal with all known fields plus at least one repeated field. We go +2048
  // here based on observations on a large 4GB android trace. This is to avoid
  // trivial re-allocations when dealing with repeated fields of a message that
  // has > INITIAL_STACK_CAPACITY fields.
  const uint32_t min_capacity = num_fields_ + 2048;  // Any num >= +1 will do.
  const uint32_t new_capacity = std::max(capacity_ * 2, min_capacity);
  PERFETTO_CHECK(new_capacity > size_ && new_capacity > num_fields_);
  std::unique_ptr<Field[]> new_storage(new Field[new_capacity]);

  static_assert(std::is_trivially_constructible<Field>::value,
                "Field must be trivially constructible");
  static_assert(std::is_trivially_copyable<Field>::value,
                "Field must be trivially copyable");

  // There is no need to initialize the slots for known field IDs: reads of
  // them are gated on the presence bitmap. There is also no need to initialize
  // the repeated slots, because they are written linearly with no gaps and are
  // always initialized before incrementing |size_|.
  const uint32_t new_size = std::max(size_, num_fields_);
  memcpy(&new_storage[0], fields_, sizeof(Field) * size_);

  heap_storage_ = std::move(new_storage);
  fields_ = &heap_storage_[0];
  capacity_ = new_capacity;
  size_ = new_size;
}

}  // namespace protozero
