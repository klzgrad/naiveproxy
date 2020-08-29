// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_index_conversions.h"

#include <limits>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

uint64_t QpackAbsoluteIndexToEncoderStreamRelativeIndex(
    uint64_t absolute_index,
    uint64_t inserted_entry_count) {
  DCHECK_LT(absolute_index, inserted_entry_count);

  return inserted_entry_count - absolute_index - 1;
}

uint64_t QpackAbsoluteIndexToRequestStreamRelativeIndex(uint64_t absolute_index,
                                                        uint64_t base) {
  DCHECK_LT(absolute_index, base);

  return base - absolute_index - 1;
}

bool QpackEncoderStreamRelativeIndexToAbsoluteIndex(
    uint64_t relative_index,
    uint64_t inserted_entry_count,
    uint64_t* absolute_index) {
  if (relative_index >= inserted_entry_count) {
    return false;
  }

  *absolute_index = inserted_entry_count - relative_index - 1;
  return true;
}

bool QpackRequestStreamRelativeIndexToAbsoluteIndex(uint64_t relative_index,
                                                    uint64_t base,
                                                    uint64_t* absolute_index) {
  if (relative_index >= base) {
    return false;
  }

  *absolute_index = base - relative_index - 1;
  return true;
}

bool QpackPostBaseIndexToAbsoluteIndex(uint64_t post_base_index,
                                       uint64_t base,
                                       uint64_t* absolute_index) {
  if (post_base_index >= std::numeric_limits<uint64_t>::max() - base) {
    return false;
  }

  *absolute_index = base + post_base_index;
  return true;
}

}  // namespace quic
