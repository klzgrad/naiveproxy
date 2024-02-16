// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_UDP_SOCKET_PLATFORM_API_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_UDP_SOCKET_PLATFORM_API_H_

#include "quiche_platform_impl/quiche_udp_socket_platform_impl.h"

#include "quiche/quic/core/quic_types.h"
#include "quiche/common/quiche_ip_address_family.h"

namespace quiche {

const size_t kCmsgSpaceForGooglePacketHeader =
    kCmsgSpaceForGooglePacketHeaderImpl;

inline bool GetGooglePacketHeadersFromControlMessage(
    struct ::cmsghdr* cmsg, char** packet_headers, size_t* packet_headers_len) {
  return GetGooglePacketHeadersFromControlMessageImpl(cmsg, packet_headers,
                                                      packet_headers_len);
}

inline void SetGoogleSocketOptions(int fd) { SetGoogleSocketOptionsImpl(fd); }

// Retrieves the IP TOS byte for |fd| and |address_family|, based on the correct
// sockopt for the platform, replaces the two ECN bits of that byte with the
// value in |ecn_codepoint|.
// The result is stored in |value| in the proper format to set the TOS byte
// using a cmsg. |value| must point to memory of size |value_len|. Stores the
// correct cmsg type to use in |type|.
// Returns 0 on success. Returns EINVAL if |address_family| is neither IP_V4 nor
// IP_V6, or if |value_len| is not large enough to store the appropriately
// formatted argument. If getting the socket option fails, returns the
// associated error code.
inline int GetEcnCmsgArgsPreserveDscp(
    const int fd, const quiche::IpAddressFamily address_family,
    quic::QuicEcnCodepoint ecn_codepoint, int& type, void* value,
    socklen_t& value_len) {
  return GetEcnCmsgArgsPreserveDscpImpl(
      fd, ToPlatformAddressFamily(address_family),
      static_cast<uint8_t>(ecn_codepoint), type, value, value_len);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_UDP_SOCKET_PLATFORM_API_H_
