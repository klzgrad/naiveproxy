// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_STR_CAT_H_
#define NET_QUIC_PLATFORM_API_QUIC_STR_CAT_H_

#include <string>
#include <utility>

#include "net/quic/platform/impl/quic_str_cat_impl.h"

namespace net {

template <typename... Args>
inline std::string QuicStrCat(const Args&... args) {
  return QuicStrCatImpl(std::forward<const Args&>(args)...);
}

template <typename... Args>
inline std::string QuicStringPrintf(const Args&... args) {
  return QuicStringPrintfImpl(std::forward<const Args&>(args)...);
}

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_STR_CAT_H_
