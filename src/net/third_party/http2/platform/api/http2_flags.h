// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_

#include "net/third_party/http2/platform/impl/http2_flags_impl.h"

#define GetHttp2ReloadableFlag(flag) GetHttp2ReloadableFlagImpl(flag)
#define SetHttp2ReloadableFlag(flag, value) \
  SetHttp2ReloadableFlagImpl(flag, value)

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_
