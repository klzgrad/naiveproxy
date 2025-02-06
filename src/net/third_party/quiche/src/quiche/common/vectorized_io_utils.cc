// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/vectorized_io_utils.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "absl/base/optimization.h"
#include "absl/base/prefetch.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace quiche {

size_t TotalStringViewSpanSize(absl::Span<const absl::string_view> span) {
  size_t total = 0;
  for (absl::string_view view : span) {
    total += view.size();
  }
  return total;
}

size_t GatherStringViewSpan(absl::Span<const absl::string_view> inputs,
                            absl::Span<char> output) {
  size_t bytes_copied = 0;
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (inputs[i].empty()) {
      continue;
    }
    const size_t bytes_to_copy = std::min(inputs[i].size(), output.size());
    if (bytes_to_copy == 0) {
      break;
    }
    const absl::Span<char> next_output = output.subspan(bytes_to_copy);

    // Prefetch the first two lines of the next input; the hardware prefetcher
    // is expected to take care of the rest.
    if (!next_output.empty() && (i + 1) < inputs.size() &&
        !inputs[i + 1].empty()) {
      absl::PrefetchToLocalCache(&inputs[i + 1][0]);
      if (inputs[i + 1].size() > ABSL_CACHELINE_SIZE) {
        absl::PrefetchToLocalCache(&inputs[i + 1][ABSL_CACHELINE_SIZE]);
      }
    }

    memcpy(output.data(), inputs[i].data(), bytes_to_copy);
    bytes_copied += bytes_to_copy;
    output = next_output;
  }
  return bytes_copied;
}

}  // namespace quiche
