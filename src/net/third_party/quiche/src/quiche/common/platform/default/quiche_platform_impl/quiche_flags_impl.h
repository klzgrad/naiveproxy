// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_FLAGS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_FLAGS_IMPL_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "quiche/common/platform/api/quiche_export.h"

#define QUIC_FLAG(flag, value) QUICHE_EXPORT_PRIVATE extern bool flag;
#include "quiche/quic/core/quic_flags_list.h"
#undef QUIC_FLAG

inline bool GetQuicheFlagImpl(bool flag) { return flag; }
inline int32_t GetQuicheFlagImpl(int32_t flag) { return flag; }
inline int64_t GetQuicheFlagImpl(int64_t flag) { return flag; }
inline uint64_t GetQuicheFlagImpl(uint64_t flag) { return flag; }
inline double GetQuicheFlagImpl(double flag) { return flag; }
inline std::string GetQuicheFlagImpl(const std::string& flag) { return flag; }
#define SetQuicheFlagImpl(flag, value) ((flag) = (value))

// ------------------------------------------------------------------------
// QUIC feature flags implementation.
// ------------------------------------------------------------------------
#define QUICHE_RELOADABLE_FLAG(flag) FLAGS_quic_reloadable_flag_##flag
#define QUICHE_RESTART_FLAG(flag) FLAGS_quic_restart_flag_##flag
#define GetQuicheReloadableFlagImpl(module, flag) \
  GetQuicheFlag(QUICHE_RELOADABLE_FLAG(flag))
#define SetQuicheReloadableFlagImpl(module, flag, value) \
  SetQuicheFlag(QUICHE_RELOADABLE_FLAG(flag), value)
#define GetQuicheRestartFlagImpl(module, flag) \
  GetQuicheFlag(QUICHE_RESTART_FLAG(flag))
#define SetQuicheRestartFlagImpl(module, flag, value) \
  SetQuicheFlag(QUICHE_RESTART_FLAG(flag), value)

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_FLAGS_IMPL_H_
