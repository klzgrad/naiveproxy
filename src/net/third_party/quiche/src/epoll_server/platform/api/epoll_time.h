// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_TIME_H_
#define QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_TIME_H_

#include "net/tools/epoll_server/platform/impl/epoll_time_impl.h"

namespace epoll_server {

inline int64_t WallTimeNowInUsec() { return WallTimeNowInUsecImpl(); }

}  // namespace epoll_server

#endif  // QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_TIME_H_
