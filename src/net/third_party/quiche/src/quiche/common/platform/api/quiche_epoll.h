// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_EPOLL_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_EPOLL_H_

#include "quiche_platform_impl/quiche_epoll_impl.h"

namespace quiche {

using QuicheEpollServer = QuicheEpollServerImpl;
using QuicheEpollEvent = QuicheEpollEventImpl;
using QuicheEpollAlarmBase = QuicheEpollAlarmBaseImpl;
using QuicheEpollCallbackInterface = QuicheEpollCallbackInterfaceImpl;

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_EPOLL_H_
