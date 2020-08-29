// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_

#include "net/http2/platform/impl/http2_flags_impl.h"

#define GetHttp2ReloadableFlag(flag) GetHttp2ReloadableFlagImpl(flag)
#define SetHttp2ReloadableFlag(flag, value) \
  SetHttp2ReloadableFlagImpl(flag, value)

#define HTTP2_CODE_COUNT_N HTTP2_CODE_COUNT_N_IMPL

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_
