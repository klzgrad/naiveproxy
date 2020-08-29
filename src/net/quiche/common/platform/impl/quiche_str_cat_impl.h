// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_STR_CAT_IMPL_H_
#define NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_STR_CAT_IMPL_H_

#include <sstream>
#include <string>
#include <utility>

#include "base/strings/stringprintf.h"

namespace quiche {

template <typename... Args>
inline std::string QuicheStrCatImpl(const Args&... args) {
  std::ostringstream oss;
  int dummy[] = {1, (oss << args, 0)...};
  static_cast<void>(dummy);
  return oss.str();
}

template <typename... Args>
inline std::string QuicheStringPrintfImpl(const Args&... args) {
  return base::StringPrintf(std::forward<const Args&>(args)...);
}

}  // namespace quiche

#endif  // NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_STR_CAT_IMPL_H_
