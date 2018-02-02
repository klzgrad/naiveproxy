// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_SOCKADDR_STORAGE_H_
#define NET_BASE_SOCKADDR_STORAGE_H_

#include "build/build_config.h"

#if defined(OS_POSIX)
#include <sys/socket.h>
#include <sys/types.h>
#elif defined(OS_WIN)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "net/base/net_export.h"

namespace net {

// Convenience struct for when you need a |struct sockaddr|.
struct NET_EXPORT SockaddrStorage {
  SockaddrStorage();
  SockaddrStorage(const SockaddrStorage& other);
  void operator=(const SockaddrStorage& other);

  struct sockaddr_storage addr_storage;
  socklen_t addr_len;
  struct sockaddr* const addr;
};

}  // namespace net

#endif  // NET_BASE_SOCKADDR_STORAGE_H_
