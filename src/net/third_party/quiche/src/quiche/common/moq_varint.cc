// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/moq_varint.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "absl/base/casts.h"
#include "absl/numeric/bits.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/quiche_endian.h"

namespace quiche {
namespace {

constexpr size_t kTheForbiddenMoqVarintLength = 7;
constexpr uint64_t kMaxEbmlVarintValue = (UINT64_C(1) << 56) - 2;

// The two varint dialects are similar, the notable differences are:
//   - MOQ varints use a sequence of 1s for encoding length, EBML uses 0s.
//   - MOQ does not allow varints of length 7.
//   - EBML does not allow varints of length 9, meaning only 2^56 values are
//         encodable.
enum class VarintDialect { kMoq, kEbml };

template <VarintDialect dialect>
size_t GetVarintSizeForFirstByte(char first_byte_raw) {
  uint8_t first_byte = absl::bit_cast<uint8_t>(first_byte_raw);
  if constexpr (dialect == VarintDialect::kEbml) {
    first_byte = ~first_byte;
  }
  return absl::countl_one(first_byte) + 1;
}

template <VarintDialect dialect>
std::optional<uint64_t> ReadVarint(QuicheDataReader& reader) {
  if (reader.BytesRemaining() == 0) {
    return std::nullopt;
  }
  const size_t length = GetVarintSizeForFirstByte<dialect>(reader.PeekByte());
  if (reader.BytesRemaining() < length) {
    return std::nullopt;
  }
  if (dialect == VarintDialect::kMoq &&
      length == kTheForbiddenMoqVarintLength) {
    return std::nullopt;
  }
  if (dialect == VarintDialect::kEbml && length > 8) {
    return std::nullopt;
  }
  QUICHE_DCHECK_LE(length, 9u);

  absl::string_view data = reader.PeekRemainingPayload();
  uint64_t result = 0;
  if (length == 9) {
    memcpy(&result, data.data() + 1, 8);
  } else {
    char* result_start = reinterpret_cast<char*>(&result) + (8 - length);
    memcpy(result_start, data.data(), length);
    const uint8_t first_byte_mask = (1 << (8 - length)) - 1;
    *result_start &= first_byte_mask;
  }
  result = QuicheEndian::NetToHost64(result);
  reader.Seek(length);
  return result;
}

template <VarintDialect dialect>
size_t GetVarintLengthForValue(uint64_t value) {
  if (dialect == VarintDialect::kEbml) {
    // Avoid writing "all ones" sequences, as those have magic semantics in some
    // contexts (RFC 8794, Section 6.2).
    ++value;
  }
  // This is equivalent to `ceil(bit_width(value) / 7)`.
  size_t length = (6 + absl::bit_width(value)) / 7;
  if (dialect == VarintDialect::kMoq &&
      length == kTheForbiddenMoqVarintLength) {
    ++length;
  }
  return std::clamp<size_t>(length, 1, 9);
}

template <VarintDialect dialect>
[[nodiscard]] bool WriteVarint(QuicheDataWriter& writer, uint64_t value,
                               size_t length) {
  if (dialect == VarintDialect::kEbml && value > kMaxEbmlVarintValue) {
    return false;
  }
  QUICHE_DCHECK_GE(length, 1u);
  QUICHE_DCHECK_LE(length, 9u);

  if (length == 9) {
    QUICHE_DCHECK(dialect == VarintDialect::kMoq);
    return writer.WriteUInt8(0xff) && writer.WriteUInt64(value);
  }

  char buffer[8];
  value = QuicheEndian::HostToNet64(value);
  const char* input_start =
      reinterpret_cast<const char*>(&value) + (8 - length);
  memcpy(buffer, input_start, length);
  const uint8_t first_byte_data_mask = (1 << (8 - length)) - 1;
  const uint8_t first_byte_length_mask = ~first_byte_data_mask;
  uint8_t first_byte_length = 1 << (8 - length);
  if constexpr (dialect == VarintDialect::kMoq) {
    first_byte_length = ~first_byte_length;
  }
  buffer[0] = (buffer[0] & first_byte_data_mask) |
              (first_byte_length & first_byte_length_mask);
  return writer.WriteBytes(buffer, length);
}

}  // namespace

size_t GetMoqVarintLengthForFirstByte(char first_byte) {
  return GetVarintSizeForFirstByte<VarintDialect::kMoq>(first_byte);
}
size_t GetEbmlVarintLengthForFirstByte(char first_byte) {
  return GetVarintSizeForFirstByte<VarintDialect::kEbml>(first_byte);
}

size_t GetMoqVarintLengthForValue(uint64_t value) {
  return GetVarintLengthForValue<VarintDialect::kMoq>(value);
}
std::optional<size_t> GetEbmlVarintLengthForValue(uint64_t value) {
  if (value > kMaxEbmlVarintValue) {
    return std::nullopt;
  }
  return GetVarintLengthForValue<VarintDialect::kEbml>(value);
}

std::optional<uint64_t> ReadMoqVarint(QuicheDataReader& reader) {
  return ReadVarint<VarintDialect::kMoq>(reader);
}
std::optional<uint64_t> ReadEbmlVarint(QuicheDataReader& reader) {
  return ReadVarint<VarintDialect::kEbml>(reader);
}

[[nodiscard]] bool WriteMoqVarint(QuicheDataWriter& writer, uint64_t value) {
  const size_t length = GetVarintLengthForValue<VarintDialect::kMoq>(value);
  return WriteVarint<VarintDialect::kMoq>(writer, value, length);
}
[[nodiscard]] bool WriteEbmlVarint(QuicheDataWriter& writer, uint64_t value) {
  const size_t length = GetVarintLengthForValue<VarintDialect::kEbml>(value);
  return WriteVarint<VarintDialect::kEbml>(writer, value, length);
}
[[nodiscard]] bool WriteMoqVarintWithCustomLength(QuicheDataWriter& writer,
                                                  uint64_t value,
                                                  size_t length) {
  return WriteVarint<VarintDialect::kMoq>(writer, value, length);
}

}  // namespace quiche
