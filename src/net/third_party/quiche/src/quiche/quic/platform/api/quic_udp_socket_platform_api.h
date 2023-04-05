// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_UDP_SOCKET_PLATFORM_API_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_UDP_SOCKET_PLATFORM_API_H_

#include "quiche/common/platform/api/quiche_udp_socket_platform_api.h"

namespace quic {

const size_t kCmsgSpaceForGooglePacketHeader =
    quiche::kCmsgSpaceForGooglePacketHeader;

inline bool GetGooglePacketHeadersFromControlMessage(
    struct ::cmsghdr* cmsg, char** packet_headers, size_t* packet_headers_len) {
  return quiche::GetGooglePacketHeadersFromControlMessage(cmsg, packet_headers,
                                                          packet_headers_len);
}

inline void SetGoogleSocketOptions(int fd) {
  quiche::SetGoogleSocketOptions(fd);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_UDP_SOCKET_PLATFORM_API_H_
