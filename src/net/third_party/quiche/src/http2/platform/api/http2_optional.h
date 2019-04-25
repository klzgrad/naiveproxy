// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_OPTIONAL_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_OPTIONAL_H_

#include <utility>

#include "net/http2/platform/impl/http2_optional_impl.h"

namespace http2 {

template <typename T>
using Http2Optional = Http2OptionalImpl<T>;

}  // namespace http2

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_OPTIONAL_H_
