// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAGS_IMPL_H_
#define NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAGS_IMPL_H_

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

QUICHE_EXPORT_PRIVATE extern bool FLAGS_http2_varint_decode_64_bits;
QUICHE_EXPORT_PRIVATE extern bool FLAGS_http2_skip_querying_entry_buffer_error;

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

#define HTTP2_CODE_COUNT_N_IMPL(flag, instance, total) \
  do {                                                 \
  } while (0)

}  // namespace http2

#endif  // NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAGS_IMPL_H_
