// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_STR_CAT_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_STR_CAT_H_

#include <string>
#include <utility>

#include "net/quiche/common/platform/impl/quiche_str_cat_impl.h"

namespace quiche {

// Merges given strings or numbers with no delimiter.
template <typename... Args>
inline std::string QuicheStrCat(const Args&... args) {
  return quiche::QuicheStrCatImpl(std::forward<const Args&>(args)...);
}

template <typename... Args>
inline std::string QuicheStringPrintf(const Args&... args) {
  return QuicheStringPrintfImpl(std::forward<const Args&>(args)...);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_STR_CAT_H_
