// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_TCP_SOCKET_PROXY_H_
#define NET_TEST_TCP_SOCKET_PROXY_H_

#include <stdint.h>

#include <memory>

#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace net {

class IPEndPoint;

// TcpSocketProxy proxies TCP connection from localhost to a remote IP address.
class TcpSocketProxy {
 public:
  explicit TcpSocketProxy(
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner);
  ~TcpSocketProxy();

  // Initializes local socket for the proxy. If |local_port| is not 0 then the
  // proxy will listen on that port. Otherwise the socket will be bound to an
  // available port and local_port() should be used to get the port number.
  // Returns false if initialization fails.
  bool Initialize(int local_port = 0);

  // Local port number for the proxy or 0 if the proxy is not initialized.
  uint16_t local_port() const { return local_port_; }

  // Starts the proxy for the specified |remote_endpoint|. Must be called after
  // a successful Initialize() call and before any incoming connection on
  // local_port() are initiated. Port number in |remote_endpoint| may be
  // different from local_port().
  void Start(const IPEndPoint& remote_endpoint);

 private:
  class Core;

  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;

  // Core implements the proxy functionality. It runs on |io_task_runner_|.
  std::unique_ptr<Core> core_;

  uint16_t local_port_ = 0;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(TcpSocketProxy);
};

}  // namespace net

#endif  // NET_TEST_TCP_SOCKET_PROXY_H_
