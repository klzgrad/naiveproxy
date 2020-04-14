// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_MOCK_KERNEL_H_
#define QUICHE_QUIC_QBONE_PLATFORM_MOCK_KERNEL_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/platform/kernel_interface.h"

namespace quic {

class MockKernel : public KernelInterface {
 public:
  MockKernel() {}

  MOCK_METHOD3(bind,
               int(int fd, const struct sockaddr* addr, socklen_t addr_len));
  MOCK_METHOD1(close, int(int fd));
  MOCK_METHOD3(ioctl, int(int fd, int request, void* argp));
  MOCK_METHOD2(open, int(const char* pathname, int flags));
  MOCK_METHOD3(read, ssize_t(int fd, void* buf, size_t count));
  MOCK_METHOD6(recvfrom,
               ssize_t(int sockfd,
                       void* buf,
                       size_t len,
                       int flags,
                       struct sockaddr* src_addr,
                       socklen_t* addrlen));
  MOCK_METHOD3(sendmsg,
               ssize_t(int sockfd, const struct msghdr* msg, int flags));
  MOCK_METHOD6(sendto,
               ssize_t(int sockfd,
                       const void* buf,
                       size_t len,
                       int flags,
                       const struct sockaddr* dest_addr,
                       socklen_t addrlen));
  MOCK_METHOD3(socket, int(int domain, int type, int protocol));
  MOCK_METHOD5(setsockopt, int(int, int, int, const void*, socklen_t));
  MOCK_METHOD3(write, ssize_t(int fd, const void* buf, size_t count));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_MOCK_KERNEL_H_
