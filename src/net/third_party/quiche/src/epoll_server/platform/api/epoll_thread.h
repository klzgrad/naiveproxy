// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_THREAD_H_
#define QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_THREAD_H_

#include <string>

#include "net/tools/epoll_server/platform/impl/epoll_thread_impl.h"

namespace epoll_server {

// A class representing a thread of execution in QUIC.
class EpollThread : public EpollThreadImpl {
 public:
  EpollThread(const std::string& string) : EpollThreadImpl(string) {}
  EpollThread(const EpollThread&) = delete;
  EpollThread& operator=(const EpollThread&) = delete;

  // Impl defines a virtual void Run() method which subclasses
  // must implement.
};

}  // namespace epoll_server

#endif  // QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_THREAD_H_
