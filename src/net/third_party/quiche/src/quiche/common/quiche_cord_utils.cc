// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_cord_utils.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"

namespace quiche {

namespace {
// Explicitly stated in the absl::Cord documentation.
constexpr size_t kMaxInlinedCordSize = 15;
}  // namespace

absl::Cord MemSliceToCord(QuicheMemSlice slice) {
  if (slice.empty()) {
    return absl::Cord();
  }
  QuicheMemSlice::ReleasedSlice released_slice = std::move(slice).Release();
  if (released_slice.callback == nullptr) {
    released_slice.callback = [](absl::string_view) {};
  }
  return absl::MakeCordFromExternal(released_slice.data,
                                    std::move(released_slice.callback));
}

absl::Cord MemSliceSpanToCord(absl::Span<QuicheMemSlice> slices) {
  absl::Cord cord;
  for (QuicheMemSlice& slice : slices) {
    cord.Append(MemSliceToCord(std::move(slice)));
  }
  return cord;
}

void CordToMemSlices(const absl::Cord& cord,
                     UnretainedCallback<void(QuicheMemSlice)> sink) {
  size_t current_offset = 0;
  for (absl::string_view chunk : cord.Chunks()) {
    // absl::Cord does not provide any API to access individual chunks or to
    // extract the release callback from a chunk. To side-step this issue,
    // allocate an instance of absl::Cord on the heap, and delete it from the
    // QuicheMemSlice release callback.  The heap allocation is necessary since
    // absl::Cord supports small string inlining, meaning the resulting
    // absl::string_view is not guaranteed to point to a stable heap-allocated
    // address otherwise.
    auto subcord = std::make_unique<absl::Cord>(
        cord.Subcord(current_offset, chunk.size()));
    QUICHE_DCHECK(subcord->TryFlat().has_value());
    // Despite the QUICHE_DCHECK above, the production code below still uses
    // `Flatten()` as a fail-safe.
    absl::string_view stored_chunk = subcord->Flatten();

    QUICHE_DCHECK_EQ(stored_chunk, chunk);
    if (chunk.size() > kMaxInlinedCordSize) {
      // absl::Cord has a documented inlining threshold; ensure that if it is
      // exceeded, the data address does not change, since the goal of this API
      // is to avoid copies.
      QUICHE_DCHECK_EQ(reinterpret_cast<uintptr_t>(stored_chunk.data()),
                       reinterpret_cast<uintptr_t>(chunk.data()));
    }
    sink(QuicheMemSlice(
        stored_chunk.data(), stored_chunk.size(),
        [ptr = subcord.release()](absl::string_view) { delete ptr; }));
    current_offset += chunk.size();
  }
}

}  // namespace quiche
