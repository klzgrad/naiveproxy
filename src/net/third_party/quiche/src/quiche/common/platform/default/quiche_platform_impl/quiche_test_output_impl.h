// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TEST_OUTPUT_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TEST_OUTPUT_IMPL_H_

#include <string>

#include "absl/strings/string_view.h"

namespace quiche {

inline void QuicheSaveTestOutputImpl(absl::string_view /*filename*/,
                                     absl::string_view /*data*/) {}

inline bool QuicheLoadTestOutputImpl(absl::string_view /*filename*/,
                                     std::string* /*data*/) {
  return false;
}

inline void QuicheRecordTraceImpl(absl::string_view /*identifier*/,
                                  absl::string_view /*data*/) {}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TEST_OUTPUT_IMPL_H_
