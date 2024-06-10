// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_UDP_SOCKET_PLATFORM_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_UDP_SOCKET_PLATFORM_IMPL_H_

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>

namespace quiche {

constexpr size_t kCmsgSpaceForGooglePacketHeaderImpl = 0;

constexpr uint8_t kQuichePlatformImplEcnMask = 0x03;

inline bool GetGooglePacketHeadersFromControlMessageImpl(
    struct ::cmsghdr* /*cmsg*/, char** /*packet_headers*/,
    size_t* /*packet_headers_len*/) {
  return false;
}

inline void SetGoogleSocketOptionsImpl(int /*fd*/) {}

// The default implementation assigns ECN correctly given Linux socket APIs.
// TODO(b/273081493): Implement Windows socket API calls.
inline int GetEcnCmsgArgsPreserveDscpImpl(const int fd,
                                          const int address_family,
                                          uint8_t ecn_codepoint, int& type,
                                          void* value, socklen_t& value_len) {
  if ((address_family != AF_INET && address_family != AF_INET6) ||
      (ecn_codepoint & kQuichePlatformImplEcnMask) != ecn_codepoint) {
    return -EINVAL;
  }
  if (value_len < sizeof(int)) {
    return -EINVAL;
  }
  int* arg = static_cast<int*>(value);
  if (getsockopt(fd, (address_family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6,
                 (address_family == AF_INET) ? IP_TOS : IPV6_TCLASS, arg,
                 &value_len) != 0) {
    return -1 * errno;
  }
  *arg &= static_cast<int>(~kQuichePlatformImplEcnMask);
  *arg |= static_cast<int>(ecn_codepoint);
  type = (address_family == AF_INET) ? IP_TOS : IPV6_TCLASS;
  return 0;
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_UDP_SOCKET_PLATFORM_IMPL_H_
