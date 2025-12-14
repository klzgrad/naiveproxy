// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/deterministic_connection_id_generator.h"

#include <optional>

#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

DeterministicConnectionIdGenerator::DeterministicConnectionIdGenerator(
    uint8_t expected_connection_id_length)
    : expected_connection_id_length_(expected_connection_id_length) {
  if (expected_connection_id_length_ >
      kQuicMaxConnectionIdWithLengthPrefixLength) {
    QUIC_BUG(quic_bug_465151159_01)
        << "Issuing connection IDs longer than allowed in RFC9000";
  }
}

std::optional<QuicConnectionId>
DeterministicConnectionIdGenerator::GenerateNextConnectionId(
    const QuicConnectionId& original) {
  if (expected_connection_id_length_ == 0) {
    return EmptyQuicConnectionId();
  }
  const uint64_t connection_id_hash64 = QuicUtils::FNV1a_64_Hash(
      absl::string_view(original.data(), original.length()));
  if (expected_connection_id_length_ <= sizeof(uint64_t)) {
    return QuicConnectionId(
        reinterpret_cast<const char*>(&connection_id_hash64),
        expected_connection_id_length_);
  }
  char new_connection_id_data[255] = {};
  const absl::uint128 connection_id_hash128 = QuicUtils::FNV1a_128_Hash(
      absl::string_view(original.data(), original.length()));
  static_assert(sizeof(connection_id_hash64) + sizeof(connection_id_hash128) <=
                    sizeof(new_connection_id_data),
                "bad size");
  memcpy(new_connection_id_data, &connection_id_hash64,
         sizeof(connection_id_hash64));
  // TODO(martinduke): We don't have any test coverage of the line below. In
  // particular, if the memcpy somehow misses a byte, a test could check if one
  // byte position in generated connection IDs is always the same.
  memcpy(new_connection_id_data + sizeof(connection_id_hash64),
         &connection_id_hash128, sizeof(connection_id_hash128));
  return QuicConnectionId(new_connection_id_data,
                          expected_connection_id_length_);
}

std::optional<QuicConnectionId>
DeterministicConnectionIdGenerator::MaybeReplaceConnectionId(
    const QuicConnectionId& original, const ParsedQuicVersion& version) {
  if (original.length() == expected_connection_id_length_) {
    return std::optional<QuicConnectionId>();
  }
  QUICHE_DCHECK(version.AllowsVariableLengthConnectionIds());
  std::optional<QuicConnectionId> new_connection_id =
      GenerateNextConnectionId(original);
  // Verify that ReplaceShortServerConnectionId is deterministic.
  if (!new_connection_id.has_value()) {
    QUIC_BUG(unset_next_connection_id);
    return std::nullopt;
  }
  QUICHE_DCHECK_EQ(
      *new_connection_id,
      static_cast<QuicConnectionId>(*GenerateNextConnectionId(original)));
  QUICHE_DCHECK_EQ(expected_connection_id_length_, new_connection_id->length());
  QUIC_DLOG(INFO) << "Replacing incoming connection ID " << original << " with "
                  << *new_connection_id;
  return new_connection_id;
}

}  // namespace quic
