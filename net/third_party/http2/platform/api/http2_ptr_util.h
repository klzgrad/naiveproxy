// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_PTR_UTIL_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_PTR_UTIL_H_

#include <memory>
#include <utility>

#include "net/third_party/http2/platform/impl/http2_ptr_util_impl.h"

namespace http2 {

template <typename T, typename... Args>
std::unique_ptr<T> Http2MakeUnique(Args&&... args) {
  return Http2MakeUniqueImpl<T>(std::forward<Args>(args)...);
}

}  // namespace http2

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_PTR_UTIL_H_
