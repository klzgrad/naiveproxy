// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_PTR_UTIL_H_
#define QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_PTR_UTIL_H_

#include <memory>

#include "net/tools/epoll_server/platform/impl/epoll_ptr_util_impl.h"

namespace epoll_server {

template <typename T, typename... Args>
std::unique_ptr<T> EpollMakeUnique(Args&&... args) {
  return EpollMakeUniqueImpl<T>(std::forward<Args>(args)...);
}

}  // namespace epoll_server

#endif  // QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_PTR_UTIL_H_
