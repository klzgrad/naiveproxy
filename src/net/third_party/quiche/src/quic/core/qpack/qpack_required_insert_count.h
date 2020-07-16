// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_REQUIRED_INSERT_COUNT_H_
#define QUICHE_QUIC_CORE_QPACK_REQUIRED_INSERT_COUNT_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// Calculate Encoded Required Insert Count from Required Insert Count and
// MaxEntries according to
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#ric.
QUIC_EXPORT_PRIVATE uint64_t
QpackEncodeRequiredInsertCount(uint64_t required_insert_count,
                               uint64_t max_entries);

// Calculate Required Insert Count from Encoded Required Insert Count,
// MaxEntries, and total number of dynamic table insertions according to
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#ric.  Returns true
// on success, false on invalid input or overflow/underflow.
QUIC_EXPORT_PRIVATE bool QpackDecodeRequiredInsertCount(
    uint64_t encoded_required_insert_count,
    uint64_t max_entries,
    uint64_t total_number_of_inserts,
    uint64_t* required_insert_count);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_REQUIRED_INSERT_COUNT_H_
