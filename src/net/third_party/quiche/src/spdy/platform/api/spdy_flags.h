// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_FLAGS_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_FLAGS_H_

#include "net/spdy/platform/impl/spdy_flags_impl.h"

#define GetSpdyReloadableFlag(flag) GetSpdyReloadableFlagImpl(flag)
#define GetSpdyRestartFlag(flag) GetSpdyRestartFlagImpl(flag)

#define SPDY_CODE_COUNT SPDY_CODE_COUNT_IMPL
#define SPDY_CODE_COUNT_N SPDY_CODE_COUNT_N_IMPL

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_FLAGS_H_
