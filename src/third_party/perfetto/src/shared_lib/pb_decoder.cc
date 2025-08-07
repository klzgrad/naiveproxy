/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "perfetto/public/abi/pb_decoder_abi.h"

#include <limits>
#include <type_traits>

#include "perfetto/public/pb_utils.h"

namespace {
template <typename T>
bool Uint64RepresentableIn(uint64_t val) {
  return val <= std::numeric_limits<T>::max();
}
}  // namespace

struct PerfettoPbDecoderField PerfettoPbDecoderParseField(
    struct PerfettoPbDecoder* decoder) {
  struct PerfettoPbDecoderField field;
  const uint8_t* read_ptr = decoder->read_ptr;
  if (read_ptr >= decoder->end_ptr) {
    field.status = PERFETTO_PB_DECODER_DONE;
    return field;
  }
  field.status = PERFETTO_PB_DECODER_ERROR;
  uint64_t tag;
  const uint8_t* end_of_tag =
      PerfettoPbParseVarInt(read_ptr, decoder->end_ptr, &tag);
  if (end_of_tag == read_ptr) {
    field.status = PERFETTO_PB_DECODER_ERROR;
    return field;
  }
  read_ptr = end_of_tag;
  constexpr uint8_t kFieldTypeNumBits = 3;

  field.wire_type = tag & ((1 << kFieldTypeNumBits) - 1);
  uint64_t id = tag >> kFieldTypeNumBits;
  static_assert(std::is_same<uint32_t, decltype(field.id)>::value);
  if (id > std::numeric_limits<uint32_t>::max()) {
    field.status = PERFETTO_PB_DECODER_ERROR;
    return field;
  }
  field.id = static_cast<uint32_t>(id);

  switch (field.wire_type) {
    case PERFETTO_PB_WIRE_TYPE_DELIMITED: {
      uint64_t len;
      const uint8_t* end_of_len =
          PerfettoPbParseVarInt(read_ptr, decoder->end_ptr, &len);
      if (end_of_len == read_ptr || !Uint64RepresentableIn<size_t>(len)) {
        field.status = PERFETTO_PB_DECODER_ERROR;
        return field;
      }
      read_ptr = end_of_len;
      field.value.delimited.len = static_cast<size_t>(len);
      field.value.delimited.start = read_ptr;
      read_ptr += len;
      if (read_ptr > decoder->end_ptr) {
        field.status = PERFETTO_PB_DECODER_ERROR;
        return field;
      }
      field.status = PERFETTO_PB_DECODER_OK;
      decoder->read_ptr = read_ptr;
      break;
    }
    case PERFETTO_PB_WIRE_TYPE_VARINT: {
      uint64_t val;
      const uint8_t* end_of_val =
          PerfettoPbParseVarInt(read_ptr, decoder->end_ptr, &val);
      if (end_of_val == read_ptr) {
        field.status = PERFETTO_PB_DECODER_ERROR;
        return field;
      }
      read_ptr = end_of_val;
      field.value.integer64 = val;
      field.status = PERFETTO_PB_DECODER_OK;
      decoder->read_ptr = read_ptr;
      break;
    }
    case PERFETTO_PB_WIRE_TYPE_FIXED32: {
      const uint8_t* end_of_val = read_ptr + sizeof(uint32_t);
      if (end_of_val > decoder->end_ptr) {
        field.status = PERFETTO_PB_DECODER_ERROR;
        return field;
      }
      // Little endian on the wire.
      field.value.integer32 = read_ptr[0] |
                              (static_cast<uint32_t>(read_ptr[1]) << 8) |
                              (static_cast<uint32_t>(read_ptr[2]) << 16) |
                              (static_cast<uint32_t>(read_ptr[3]) << 24);
      read_ptr = end_of_val;
      decoder->read_ptr = read_ptr;
      field.status = PERFETTO_PB_DECODER_OK;
      break;
    }
    case PERFETTO_PB_WIRE_TYPE_FIXED64: {
      const uint8_t* end_of_val = read_ptr + sizeof(uint64_t);
      if (end_of_val > decoder->end_ptr) {
        field.status = PERFETTO_PB_DECODER_ERROR;
        return field;
      }
      // Little endian on the wire.
      field.value.integer64 = read_ptr[0] |
                              (static_cast<uint64_t>(read_ptr[1]) << 8) |
                              (static_cast<uint64_t>(read_ptr[2]) << 16) |
                              (static_cast<uint64_t>(read_ptr[3]) << 24) |
                              (static_cast<uint64_t>(read_ptr[4]) << 32) |
                              (static_cast<uint64_t>(read_ptr[5]) << 40) |
                              (static_cast<uint64_t>(read_ptr[6]) << 48) |
                              (static_cast<uint64_t>(read_ptr[7]) << 56);
      read_ptr = end_of_val;
      decoder->read_ptr = read_ptr;
      field.status = PERFETTO_PB_DECODER_OK;
      break;
    }
    default:
      field.status = PERFETTO_PB_DECODER_ERROR;
      return field;
  }
  return field;
}

uint32_t PerfettoPbDecoderSkipField(struct PerfettoPbDecoder* decoder) {
  const uint8_t* read_ptr = decoder->read_ptr;
  if (read_ptr >= decoder->end_ptr) {
    return PERFETTO_PB_DECODER_DONE;
  }
  uint64_t tag;
  const uint8_t* end_of_tag =
      PerfettoPbParseVarInt(decoder->read_ptr, decoder->end_ptr, &tag);
  if (end_of_tag == read_ptr) {
    return PERFETTO_PB_DECODER_ERROR;
  }
  read_ptr = end_of_tag;
  constexpr uint8_t kFieldTypeNumBits = 3;

  uint32_t wire_type = tag & ((1 << kFieldTypeNumBits) - 1);
  switch (wire_type) {
    case PERFETTO_PB_WIRE_TYPE_DELIMITED: {
      uint64_t len;
      const uint8_t* end_of_len =
          PerfettoPbParseVarInt(read_ptr, decoder->end_ptr, &len);
      if (end_of_len == read_ptr) {
        return PERFETTO_PB_DECODER_ERROR;
      }
      read_ptr = end_of_len;
      read_ptr += len;
      if (read_ptr > decoder->end_ptr) {
        return PERFETTO_PB_DECODER_ERROR;
      }
      decoder->read_ptr = read_ptr;
      return PERFETTO_PB_DECODER_OK;
    }
    case PERFETTO_PB_WIRE_TYPE_VARINT: {
      uint64_t val;
      const uint8_t* end_of_val =
          PerfettoPbParseVarInt(read_ptr, decoder->end_ptr, &val);
      if (end_of_val == read_ptr) {
        return PERFETTO_PB_DECODER_ERROR;
      }
      read_ptr = end_of_val;
      decoder->read_ptr = read_ptr;
      return PERFETTO_PB_DECODER_OK;
    }
    case PERFETTO_PB_WIRE_TYPE_FIXED32: {
      const uint8_t* end_of_val = read_ptr + sizeof(uint32_t);
      if (end_of_val > decoder->end_ptr) {
        return PERFETTO_PB_DECODER_ERROR;
      }
      read_ptr = end_of_val;
      decoder->read_ptr = read_ptr;
      return PERFETTO_PB_DECODER_OK;
    }
    case PERFETTO_PB_WIRE_TYPE_FIXED64: {
      const uint8_t* end_of_val = read_ptr + sizeof(uint64_t);
      if (read_ptr > decoder->end_ptr) {
        return PERFETTO_PB_DECODER_ERROR;
      }
      read_ptr = end_of_val;
      decoder->read_ptr = read_ptr;
      return PERFETTO_PB_DECODER_OK;
    }
    default:
      break;
  }
  return PERFETTO_PB_DECODER_ERROR;
}
