// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_required_insert_count.h"

#include <limits>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

uint64_t QpackEncodeRequiredInsertCount(uint64_t required_insert_count,
                                        uint64_t max_entries) {
  if (required_insert_count == 0) {
    return 0;
  }

  return required_insert_count % (2 * max_entries) + 1;
}

bool QpackDecodeRequiredInsertCount(uint64_t encoded_required_insert_count,
                                    uint64_t max_entries,
                                    uint64_t total_number_of_inserts,
                                    uint64_t* required_insert_count) {
  if (encoded_required_insert_count == 0) {
    *required_insert_count = 0;
    return true;
  }

  // |max_entries| is calculated by dividing an unsigned 64-bit integer by 32,
  // precluding all calculations in this method from overflowing.
  DCHECK_LE(max_entries, std::numeric_limits<uint64_t>::max() / 32);

  if (encoded_required_insert_count > 2 * max_entries) {
    return false;
  }

  *required_insert_count = encoded_required_insert_count - 1;
  DCHECK_LT(*required_insert_count, std::numeric_limits<uint64_t>::max() / 16);

  uint64_t current_wrapped = total_number_of_inserts % (2 * max_entries);
  DCHECK_LT(current_wrapped, std::numeric_limits<uint64_t>::max() / 16);

  if (current_wrapped >= *required_insert_count + max_entries) {
    // Required Insert Count wrapped around 1 extra time.
    *required_insert_count += 2 * max_entries;
  } else if (current_wrapped + max_entries < *required_insert_count) {
    // Decoder wrapped around 1 extra time.
    current_wrapped += 2 * max_entries;
  }

  if (*required_insert_count >
      std::numeric_limits<uint64_t>::max() - total_number_of_inserts) {
    return false;
  }

  *required_insert_count += total_number_of_inserts;

  // Prevent underflow, also disallow invalid value 0 for Required Insert Count.
  if (current_wrapped >= *required_insert_count) {
    return false;
  }

  *required_insert_count -= current_wrapped;

  return true;
}

}  // namespace quic
