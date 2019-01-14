// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_

#include <cstdint>
#include <string>

#include "net/third_party/quic/platform/api/quic_export.h"

#define QUIC_FLAG(type, flag, value) QUIC_EXPORT_PRIVATE extern type flag;
#include "net/quic/quic_flags_list.h"
#undef QUIC_FLAG

// API compatibility with new-style flags.
namespace quic {

inline bool GetQuicFlagImpl(bool flag) {
  return flag;
}
inline int32_t GetQuicFlagImpl(int32_t flag) {
  return flag;
}
inline uint32_t GetQuicFlagImpl(uint32_t flag) {
  return flag;
}
inline int64_t GetQuicFlagImpl(int64_t flag) {
  return flag;
}
inline uint64_t GetQuicFlagImpl(uint64_t flag) {
  return flag;
}
inline double GetQuicFlagImpl(double flag) {
  return flag;
}
inline std::string GetQuicFlagImpl(const std::string& flag) {
  return flag;
}

inline void SetQuicFlagImpl(bool* f, bool v) {
  *f = v;
}
inline void SetQuicFlagImpl(int32_t* f, int32_t v) {
  *f = v;
}
inline void SetQuicFlagImpl(uint32_t* f, uint32_t v) {
  *f = v;
}
inline void SetQuicFlagImpl(int64_t* f, int64_t v) {
  *f = v;
}
inline void SetQuicFlagImpl(uint64_t* f, uint64_t v) {
  *f = v;
}
inline void SetQuicFlagImpl(double* f, double v) {
  *f = v;
}
inline void SetQuicFlagImpl(std::string* f, const std::string& v) {
  *f = v;
}

// ------------------------------------------------------------------------
// // QUIC feature flags implementation.
// // ------------------------------------------------------------------------
#define RELOADABLE_FLAG(flag) FLAGS_quic_reloadable_flag_##flag
#define RESTART_FLAG(flag) FLAGS_quic_restart_flag_##flag

#define GetQuicReloadableFlagImpl(flag) GetQuicFlag(RELOADABLE_FLAG(flag))
#define SetQuicReloadableFlagImpl(flag, value) \
  SetQuicFlag(&RELOADABLE_FLAG(flag), value)
#define GetQuicRestartFlagImpl(flag) GetQuicFlag(RESTART_FLAG(flag))
#define SetQuicRestartFlagImpl(flag, value) \
  SetQuicFlag(&RESTART_FLAG(flag), value)

}  // namespace quic
#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_
