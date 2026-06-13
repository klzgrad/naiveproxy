// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains parsing and serialization logic for two closely related
// variable-length integer formats:
//  - MOQT varint (draft-ietf-moq-transport-17, Section 1.4.1)
//  - EBML varint (RFC 8794, Section 4).
//
// Both have the property that the first byte is sufficient to determine the
// length of the varint.
//
// Note that EBML defines magic varint values for indefinite lengths; those are
// not handled here when reading, but are avoided when writing.

#ifndef QUICHE_COMMON_MOQ_VARINT_H_
#define QUICHE_COMMON_MOQ_VARINT_H_

#include <cstddef>
#include <cstdint>
#include <optional>

#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"

namespace quiche {

// From the first byte of the varint, determine its total length (the first byte
// is included in that length).
size_t GetMoqVarintLengthForFirstByte(char first_byte);
size_t GetEbmlVarintLengthForFirstByte(char first_byte);

// From a value, determine the length of the minimal encoding of that value as a
// varint.
size_t GetMoqVarintLengthForValue(uint64_t value);
std::optional<size_t> GetEbmlVarintLengthForValue(uint64_t value);

// Reads the varint from `reader`.  If successful, the varint is removed from
// `data`, and the parsed value is returned.  If unsuccessful, `data` is
// unchanged and std::nullopt is returned.
std::optional<uint64_t> ReadMoqVarint(QuicheDataReader& reader);
std::optional<uint64_t> ReadEbmlVarint(QuicheDataReader& reader);

// Writes the varint into `writer`.  Returns true on success.
[[nodiscard]] bool WriteMoqVarint(QuicheDataWriter& writer, uint64_t value);
[[nodiscard]] bool WriteEbmlVarint(QuicheDataWriter& writer, uint64_t value);

// Writes the varint with the specified length instead of internally computed.
// Primarily meant to be used for testing.  If the `value` is too large to fit
// into a varint of length `length`, the result is undefined.
[[nodiscard]] bool WriteMoqVarintWithCustomLength(QuicheDataWriter& writer,
                                                  uint64_t value,
                                                  size_t length);

}  // namespace quiche

#endif  // QUICHE_COMMON_MOQ_VARINT_H_
