// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_SPAWNED_TEST_SERVER_REMOTE_TEST_SERVER_PROXY_H_
#define NET_TEST_SPAWNED_TEST_SERVER_REMOTE_TEST_SERVER_PROXY_H_

#include <stdint.h>

#include <memory>

#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace net {

class IPEndPoint;

// RemoteTestServerProxy proxies TCP connection from localhost to a remote IP
// address.
class RemoteTestServerProxy {
 public:
  RemoteTestServerProxy(
      const IPEndPoint& remote_address,
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner);
  ~RemoteTestServerProxy();

  uint16_t local_port() const { return local_port_; }

 private:
  class Core;

  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;

  // Core implements the proxy functionality. It runs on |io_task_runner_|.
  std::unique_ptr<Core> core_;

  uint16_t local_port_;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(RemoteTestServerProxy);
};

}  // namespace net

#endif  // NET_TEST_SPAWNED_TEST_SERVER_REMOTE_TEST_SERVER_PROXY_H_
