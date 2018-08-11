// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_SPDY_PLATFORM_API_SPDY_FLAGS_H_
#define NET_THIRD_PARTY_SPDY_PLATFORM_API_SPDY_FLAGS_H_

#include "net/third_party/spdy/platform/impl/spdy_flags_impl.h"

#define GetSpdyReloadableFlag(flag) GetSpdyReloadableFlagImpl(flag)
#define GetSpdyRestartFlag(flag) GetSpdyRestartFlagImpl(flag)

#endif  // NET_THIRD_PARTY_SPDY_PLATFORM_API_SPDY_FLAGS_H_
