// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_

#include <cstdint>
#include <string>

#include "net/quic/platform/api/quic_export.h"

#define QUIC_FLAG(type, flag, value) QUIC_EXPORT_PRIVATE extern type flag;
#include "net/quic/core/quic_flags_list.h"
#undef QUIC_FLAG

// API compatibility with new-style flags.
namespace net {

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

}  // namespace net
#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_
