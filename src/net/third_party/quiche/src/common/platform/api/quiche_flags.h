// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_FLAGS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_FLAGS_H_

#include "net/quiche/common/platform/impl/quiche_flags_impl.h"

#define GetQuicheReloadableFlag(module, flag) \
  GetQuicheReloadableFlagImpl(module, flag)
#define SetQuicheReloadableFlag(module, flag, value) \
  SetQuicheReloadableFlagImpl(module, flag, value)
#define GetQuicheRestartFlag(module, flag) \
  GetQuicheRestartFlagImpl(module, flag)
#define SetQuicheRestartFlag(module, flag, value) \
  SetQuicheRestartFlagImpl(module, flag, value)
#define GetQuicheFlag(flag) GetQuicheFlagImpl(flag)
#define SetQuicheFlag(flag, value) SetQuicheFlagImpl(flag, value)

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_FLAGS_H_
