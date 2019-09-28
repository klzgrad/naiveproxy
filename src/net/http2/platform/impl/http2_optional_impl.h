// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_PLATFORM_IMPL_HTTP2_OPTIONAL_IMPL_H_
#define NET_HTTP2_PLATFORM_IMPL_HTTP2_OPTIONAL_IMPL_H_

#include "base/optional.h"

namespace http2 {

template <typename T>
using Http2OptionalImpl = base::Optional<T>;

}  // namespace http2

#endif  // NET_HTTP2_PLATFORM_IMPL_HTTP2_OPTIONAL_IMPL_H_
