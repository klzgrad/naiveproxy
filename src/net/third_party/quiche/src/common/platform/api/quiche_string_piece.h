// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_STRING_PIECE_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_STRING_PIECE_H_

#include "absl/strings/string_view.h"
#include "net/quiche/common/platform/impl/quiche_string_piece_impl.h"

namespace quiche {

inline size_t QuicheHashStringPair(absl::string_view a, absl::string_view b) {
  return QuicheHashStringPairImpl(a, b);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_STRING_PIECE_H_
