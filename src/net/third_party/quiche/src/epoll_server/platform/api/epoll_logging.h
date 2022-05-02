// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_LOGGING_H_
#define QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_LOGGING_H_

#include "net/tools/epoll_server/platform/impl/epoll_logging_impl.h"

namespace epoll_server {

#define EPOLL_LOG(severity) EPOLL_LOG_IMPL(severity)
#define EPOLL_VLOG(verbosity) EPOLL_VLOG_IMPL(verbosity)
#define EPOLL_DVLOG(verbosity) EPOLL_DVLOG_IMPL(verbosity)
#define EPOLL_PLOG(severity) EPOLL_PLOG_IMPL(severity)

}  // namespace epoll_server

#endif  // QUICHE_EPOLL_SERVER_PLATFORM_API_EPOLL_LOGGING_H_
