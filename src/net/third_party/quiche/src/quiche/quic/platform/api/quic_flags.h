// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_FLAGS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_FLAGS_H_

#include <string>
#include <vector>

#include "quiche/common/platform/api/quiche_flags.h"

#define GetQuicReloadableFlag(flag) GetQuicheReloadableFlag(quic, flag)
#define SetQuicReloadableFlag(flag, value) \
  SetQuicheReloadableFlag(quic, flag, value)
#define GetQuicRestartFlag(flag) GetQuicheRestartFlag(quic, flag)
#define SetQuicRestartFlag(flag, value) SetQuicheRestartFlag(quic, flag, value)
#define GetQuicFlag(flag) GetQuicheFlag(flag)
#define SetQuicFlag(flag, value) SetQuicheFlag(flag, value)

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_FLAGS_H_
