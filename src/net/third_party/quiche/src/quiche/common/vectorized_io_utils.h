// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_VECTORIZED_IO_UTILS_H_
#define QUICHE_COMMON_VECTORIZED_IO_UTILS_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Computes the total size of all strings in the provided span.
size_t TotalStringViewSpanSize(absl::Span<const absl::string_view> span);

// Copies data contained in `inputs` into `output`, up until either the `output`
// is full or the `inputs` are copied fully; returns the actual number of bytes
// copied.
size_t QUICHE_EXPORT GatherStringViewSpan(
    absl::Span<const absl::string_view> inputs, absl::Span<char> output);

}  // namespace quiche

#endif  // QUICHE_COMMON_VECTORIZED_IO_UTILS_H_
