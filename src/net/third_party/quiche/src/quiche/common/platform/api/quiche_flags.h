// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_FLAGS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_FLAGS_H_

#include "quiche_platform_impl/quiche_flags_impl.h"

// Flags accessed via GetQuicheReloadableFlag/GetQuicheRestartFlag are temporary
// boolean flags that are used to enable or disable code behavior changes.  The
// current list is available in the quiche/quic/core/quic_flags_list.h file.
#define GetQuicheReloadableFlag(module, flag) \
  GetQuicheReloadableFlagImpl(module, flag)
#define SetQuicheReloadableFlag(module, flag, value) \
  SetQuicheReloadableFlagImpl(module, flag, value)
#define GetQuicheRestartFlag(module, flag) \
  GetQuicheRestartFlagImpl(module, flag)
#define SetQuicheRestartFlag(module, flag, value) \
  SetQuicheRestartFlagImpl(module, flag, value)

// Flags accessed via GetQuicheFlag are permanent flags used to control QUICHE
// library behavior.  The current list is available in the
// quiche/quic/core/quic_protocol_flags_list.h file.
#define GetQuicheFlag(flag) GetQuicheFlagImpl(flag)
#define SetQuicheFlag(flag, value) SetQuicheFlagImpl(flag, value)

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_FLAGS_H_
