// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_FLAGS_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_FLAGS_H_

#include "common/platform/api/quiche_flags.h"

#define GetSpdyReloadableFlag(flag) GetQuicheReloadableFlag(spdy, flag)
#define SetSpdyReloadableFlag(flag, value) \
  SetQuicheReloadableFlag(spdy, flag, value)
#define GetSpdyRestartFlag(flag) GetQuicheRestartFlag(spdy, flag)
#define SetSpdyRestartFlag(flag, value) SetQuicheRestartFlag(spdy, flag, value)
#define GetSpdyFlag(flag) GetQuicheFlag(flag)
#define SetSpdyFlag(flag, value) SetQuicheFlag(flag, value)

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_FLAGS_H_
