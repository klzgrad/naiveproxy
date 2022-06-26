// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_EPOLL_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_EPOLL_H_

#include "quiche/common/platform/api/quiche_epoll.h"

namespace quic {

using QuicEpollServer = quiche::QuicheEpollServer;
using QuicEpollEvent = quiche::QuicheEpollEvent;
using QuicEpollAlarmBase = quiche::QuicheEpollAlarmBase;
using QuicEpollCallbackInterface = quiche::QuicheEpollCallbackInterface;

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_EPOLL_H_
