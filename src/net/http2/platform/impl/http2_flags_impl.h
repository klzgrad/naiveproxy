// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAGS_IMPL_H_
#define NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAGS_IMPL_H_

#include "net/third_party/quiche/src/http2/platform/api/http2_export.h"

HTTP2_EXPORT_PRIVATE extern bool FLAGS_http2_varint_decode_64_bits;

namespace http2 {

inline bool GetHttp2FlagImpl(bool flag) {
  return flag;
}

inline void SetHttp2FlagImpl(bool* f, bool v) {
  *f = v;
}

#define HTTP2_RELOADABLE_FLAG(flag) FLAGS_##flag

#define GetHttp2ReloadableFlagImpl(flag) \
  GetHttp2FlagImpl(HTTP2_RELOADABLE_FLAG(flag))
#define SetHttp2ReloadableFlagImpl(flag, value) \
  SetHttp2FlagImpl(&HTTP2_RELOADABLE_FLAG(flag), value)

}  // namespace http2

#endif  // NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAGS_IMPL_H_
