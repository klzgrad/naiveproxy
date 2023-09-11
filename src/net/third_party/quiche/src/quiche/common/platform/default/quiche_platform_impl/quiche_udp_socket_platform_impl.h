// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_UDP_SOCKET_PLATFORM_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_UDP_SOCKET_PLATFORM_IMPL_H_

#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>

namespace quiche {

constexpr size_t kCmsgSpaceForGooglePacketHeaderImpl = 0;

inline bool GetGooglePacketHeadersFromControlMessageImpl(
    struct ::cmsghdr* /*cmsg*/, char** /*packet_headers*/,
    size_t* /*packet_headers_len*/) {
  return false;
}

inline void SetGoogleSocketOptionsImpl(int /*fd*/) {}

inline int GetEcnCmsgArgsPreserveDscpImpl(const int /*fd*/,
                                          const int /*address_family*/,
                                          uint8_t /*ecn_codepoint*/,
                                          int& /*type*/, void* /*value*/,
                                          socklen_t& /*value_len*/) {
  // TODO(b/273081493): implement this.
  return 0;
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_UDP_SOCKET_PLATFORM_IMPL_H_
