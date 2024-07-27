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

#define QUIC_FLAG(flag, value) QUICHE_EXPORT extern bool FLAGS_##flag;
#include "quiche/quic/core/quic_flags_list.h"
#undef QUIC_FLAG

// Protocol flags.
#define QUICHE_PROTOCOL_FLAG(type, flag, ...) \
  QUICHE_EXPORT extern type FLAGS_##flag;
#include "quiche/common/quiche_protocol_flags_list.h"
#undef QUICHE_PROTOCOL_FLAG

#define GetQuicheFlagImpl(flag) GetQuicheFlagImplImpl(FLAGS_##flag)
inline bool GetQuicheFlagImplImpl(bool flag) { return flag; }
inline int32_t GetQuicheFlagImplImpl(int32_t flag) { return flag; }
inline int64_t GetQuicheFlagImplImpl(int64_t flag) { return flag; }
inline uint32_t GetQuicheFlagImplImpl(uint32_t flag) { return flag; }
inline uint64_t GetQuicheFlagImplImpl(uint64_t flag) { return flag; }
inline double GetQuicheFlagImplImpl(double flag) { return flag; }
inline std::string GetQuicheFlagImplImpl(const std::string& flag) {
  return flag;
}
#define SetQuicheFlagImpl(flag, value) ((FLAGS_##flag) = (value))

// ------------------------------------------------------------------------
// QUICHE feature flags implementation.
// ------------------------------------------------------------------------
#define QUICHE_RELOADABLE_FLAG(flag) quic_reloadable_flag_##flag
#define QUICHE_RESTART_FLAG(flag) quic_restart_flag_##flag
#define GetQuicheReloadableFlagImpl(flag) \
  GetQuicheFlag(QUICHE_RELOADABLE_FLAG(flag))
#define SetQuicheReloadableFlagImpl(flag, value) \
  SetQuicheFlag(QUICHE_RELOADABLE_FLAG(flag), value)
#define GetQuicheRestartFlagImpl(flag) GetQuicheFlag(QUICHE_RESTART_FLAG(flag))
#define SetQuicheRestartFlagImpl(flag, value) \
  SetQuicheFlag(QUICHE_RESTART_FLAG(flag), value)

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_FLAGS_IMPL_H_
