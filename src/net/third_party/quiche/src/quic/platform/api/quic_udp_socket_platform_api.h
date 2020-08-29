// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_UDP_SOCKET_PLATFORM_API_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_UDP_SOCKET_PLATFORM_API_H_

#include "net/quic/platform/impl/quic_udp_socket_platform_impl.h"

namespace quic {

const size_t kCmsgSpaceForGooglePacketHeader =
    kCmsgSpaceForGooglePacketHeaderImpl;

inline bool GetGooglePacketHeadersFromControlMessage(
    struct ::cmsghdr* cmsg,
    char** packet_headers,
    size_t* packet_headers_len) {
  return GetGooglePacketHeadersFromControlMessageImpl(cmsg, packet_headers,
                                                      packet_headers_len);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_UDP_SOCKET_PLATFORM_API_H_
