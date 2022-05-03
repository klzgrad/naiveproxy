// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_

#include "common/platform/api/quiche_flags.h"

#define GetHttp2ReloadableFlag(flag) GetQuicheReloadableFlag(http2, flag)
#define SetHttp2ReloadableFlag(flag, value) \
  SetQuicheReloadableFlag(http2, flag, value)
#define GetHttp2RestartFlag(flag) GetQuicheRestartFlag(http2, flag)
#define SetHttp2RestartFlag(flag, value) \
  SetQuicheRestartFlag(http2, flag, value)
#define GetHttp2Flag(flag) GetQuicheFlag(flag)
#define SetHttp2Flag(flag, value) SetQuicheFlag(flag, value)

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_FLAGS_H_
