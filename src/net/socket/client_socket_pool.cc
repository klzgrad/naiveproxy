// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_pool.h"

#include "base/logging.h"

namespace {

// The maximum duration, in seconds, to keep unused idle persistent sockets
// alive.
// TODO(ziadh): Change this timeout after getting histogram data on how long it
// should be.
int64_t g_unused_idle_socket_timeout_s = 10;

// The maximum duration, in seconds, to keep used idle persistent sockets alive.
int64_t g_used_idle_socket_timeout_s = 300;  // 5 minutes

}  // namespace

namespace net {

// static
base::TimeDelta ClientSocketPool::unused_idle_socket_timeout() {
  return base::TimeDelta::FromSeconds(g_unused_idle_socket_timeout_s);
}

// static
void ClientSocketPool::set_unused_idle_socket_timeout(base::TimeDelta timeout) {
  DCHECK_GT(timeout.InSeconds(), 0);
  g_unused_idle_socket_timeout_s = timeout.InSeconds();
}

// static
base::TimeDelta ClientSocketPool::used_idle_socket_timeout() {
  return base::TimeDelta::FromSeconds(g_used_idle_socket_timeout_s);
}

// static
void ClientSocketPool::set_used_idle_socket_timeout(base::TimeDelta timeout) {
  DCHECK_GT(timeout.InSeconds(), 0);
  g_used_idle_socket_timeout_s = timeout.InSeconds();
}

ClientSocketPool::ClientSocketPool() = default;

ClientSocketPool::~ClientSocketPool() = default;

}  // namespace net
