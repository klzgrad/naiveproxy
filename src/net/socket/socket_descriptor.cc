// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socket_descriptor.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <ws2tcpip.h>

#include "net/base/winsock_init.h"
#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
#include <sys/socket.h>
#include <sys/types.h>
#endif

#if BUILDFLAG(IS_APPLE)
#include <unistd.h>
#endif

namespace net {

SocketDescriptor CreatePlatformSocket(int family, int type, int protocol) {
#if BUILDFLAG(IS_WIN)
  EnsureWinsockInit();
  SocketDescriptor result = ::WSASocket(family, type, protocol, nullptr, 0,
                                        WSA_FLAG_OVERLAPPED);
  if (result != kInvalidSocket && family == AF_INET6) {
    DWORD value = 0;
    if (setsockopt(result, IPPROTO_IPV6, IPV6_V6ONLY,
                   reinterpret_cast<const char*>(&value), sizeof(value))) {
      closesocket(result);
      return kInvalidSocket;
    }
  }
  return result;
#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  SocketDescriptor result = ::socket(family, type, protocol);
  return result;
#endif  // BUILDFLAG(IS_WIN)
}

}  // namespace net
