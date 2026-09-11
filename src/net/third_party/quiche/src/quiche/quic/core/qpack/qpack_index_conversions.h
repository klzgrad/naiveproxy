// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Utility methods to convert between absolute indexing (used in the dynamic
// table), relative indexing used on the encoder stream, and relative indexing
// and post-base indexing used on request streams (in header blocks).  See:
// https://rfc-editor.org/rfc/rfc9204.html#section-3.2.4
// https://rfc-editor.org/rfc/rfc9204.html#section-3.2.5
// https://rfc-editor.org/rfc/rfc9204.html#section-3.2.6

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_INDEX_CONVERSIONS_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_INDEX_CONVERSIONS_H_

#include <cstdint>

#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Conversion functions used in the encoder do not check for overflow/underflow.
// Since the maximum index is limited by maximum dynamic table capacity
// (represented on uint64_t) divided by minimum header field size (defined to be
// 32 bytes), overflow is not possible.  The caller is responsible for providing
// input that does not underflow.

QUICHE_EXPORT uint64_t QpackAbsoluteIndexToEncoderStreamRelativeIndex(
    uint64_t absolute_index, uint64_t inserted_entry_count);

QUICHE_EXPORT uint64_t QpackAbsoluteIndexToRequestStreamRelativeIndex(
    uint64_t absolute_index, uint64_t base);

// Conversion functions used in the decoder operate on input received from the
// network.  These functions return false on overflow or underflow.

QUICHE_EXPORT bool QpackEncoderStreamRelativeIndexToAbsoluteIndex(
    uint64_t relative_index, uint64_t inserted_entry_count,
    uint64_t* absolute_index);

// On success, |*absolute_index| is guaranteed to be strictly less than
// std::numeric_limits<uint64_t>::max().
QUICHE_EXPORT bool QpackRequestStreamRelativeIndexToAbsoluteIndex(
    uint64_t relative_index, uint64_t base, uint64_t* absolute_index);

// On success, |*absolute_index| is guaranteed to be strictly less than
// std::numeric_limits<uint64_t>::max().
QUICHE_EXPORT bool QpackPostBaseIndexToAbsoluteIndex(uint64_t post_base_index,
                                                     uint64_t base,
                                                     uint64_t* absolute_index);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_INDEX_CONVERSIONS_H_
