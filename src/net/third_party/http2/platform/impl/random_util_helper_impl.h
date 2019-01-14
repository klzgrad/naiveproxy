// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_RANDOM_UTIL_HELPER_IMPL_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_RANDOM_UTIL_HELPER_IMPL_H_

#include "net/third_party/http2/platform/api/http2_string.h"
#include "net/third_party/http2/platform/api/http2_string_piece.h"
#include "net/third_party/http2/tools/http2_random.h"

namespace http2 {
namespace test {

Http2String RandomStringImpl(RandomBase* random,
                             int len,
                             Http2StringPiece alphabet);
}  // namespace test
}  // namespace http2

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_RANDOM_UTIL_HELPER_IMPL_H_
