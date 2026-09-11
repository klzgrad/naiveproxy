// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_CORD_UTILS_H_
#define QUICHE_COMMON_QUICHE_CORD_UTILS_H_

#include <utility>

#include "absl/strings/cord.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"

namespace quiche {

// Converts an instance of QuicheMemSlice into an instance of absl::Cord that
// owns the underlying MemSlice.
absl::Cord QUICHE_EXPORT MemSliceToCord(QuicheMemSlice slice);
// Converts a span of MemSlices into a single absl::Cord instance.  All of the
// slices in `slices` are moved into the Cord, and are no longer valid.
absl::Cord QUICHE_EXPORT MemSliceSpanToCord(absl::Span<QuicheMemSlice> slices);

// Converts an absl::Cord into a sequence of QuicheMemSlice objects without
// copying any of the data in the Cord.  Calls `sink` for every QuicheMemSlice
// object generated.
void QUICHE_EXPORT CordToMemSlices(
    const absl::Cord& cord, UnretainedCallback<void(QuicheMemSlice)> sink);

// Converts an absl::Cord into a sequence of QuicheMemSlice objects, and appends
// them to the provided container.  The container has to provide a `push_back`
// method.
template <typename Container>
void QUICHE_NO_EXPORT CordToMemSlicesTo(const absl::Cord& cord,
                                        Container& container) {
  CordToMemSlices(cord, [&](QuicheMemSlice slice) {
    container.push_back(std::move(slice));
  });
}

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_CORD_UTILS_H_
