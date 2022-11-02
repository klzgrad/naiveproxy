// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_MOCK_KERNEL_H_
#define QUICHE_QUIC_QBONE_PLATFORM_MOCK_KERNEL_H_

#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/platform/kernel_interface.h"

namespace quic {

class MockKernel : public KernelInterface {
 public:
  MockKernel() {}

  MOCK_METHOD(int, bind, (int fd, const struct sockaddr*, socklen_t addr_len),
              (override));
  MOCK_METHOD(int, close, (int fd), (override));
  MOCK_METHOD(int, ioctl, (int fd, int request, void*), (override));
  MOCK_METHOD(int, open, (const char*, int flags), (override));
  MOCK_METHOD(ssize_t, read, (int fd, void*, size_t count), (override));
  MOCK_METHOD(ssize_t, recvfrom,
              (int sockfd, void*, size_t len, int flags, struct sockaddr*,
               socklen_t*),
              (override));
  MOCK_METHOD(ssize_t, sendmsg, (int sockfd, const struct msghdr*, int flags),
              (override));
  MOCK_METHOD(ssize_t, sendto,
              (int sockfd, const void*, size_t len, int flags,
               const struct sockaddr*, socklen_t addrlen),
              (override));
  MOCK_METHOD(int, socket, (int domain, int type, int protocol), (override));
  MOCK_METHOD(int, setsockopt, (int, int, int, const void*, socklen_t),
              (override));
  MOCK_METHOD(ssize_t, write, (int fd, const void*, size_t count), (override));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_MOCK_KERNEL_H_
