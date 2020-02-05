// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_KERNEL_INTERFACE_H_
#define QUICHE_QUIC_QBONE_PLATFORM_KERNEL_INTERFACE_H_

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <type_traits>
#include <utility>

namespace quic {

// A wrapper for making syscalls to the kernel, so that syscalls can be
// mocked during testing.
class KernelInterface {
 public:
  virtual ~KernelInterface() {}
  virtual int bind(int fd, const struct sockaddr* addr, socklen_t addr_len) = 0;
  virtual int close(int fd) = 0;
  virtual int ioctl(int fd, int request, void* argp) = 0;
  virtual int open(const char* pathname, int flags) = 0;
  virtual ssize_t read(int fd, void* buf, size_t count) = 0;
  virtual ssize_t recvfrom(int sockfd,
                           void* buf,
                           size_t len,
                           int flags,
                           struct sockaddr* src_addr,
                           socklen_t* addrlen) = 0;
  virtual ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags) = 0;
  virtual ssize_t sendto(int sockfd,
                         const void* buf,
                         size_t len,
                         int flags,
                         const struct sockaddr* dest_addr,
                         socklen_t addrlen) = 0;
  virtual int socket(int domain, int type, int protocol) = 0;
  virtual int setsockopt(int fd,
                         int level,
                         int optname,
                         const void* optval,
                         socklen_t optlen) = 0;
  virtual ssize_t write(int fd, const void* buf, size_t count) = 0;
};

// It is unfortunate to have R here, but std::result_of cannot be used.
template <typename F, typename R, typename... Params>
auto SyscallRetryOnError(R r, F f, Params&&... params)
    -> decltype(f(std::forward<Params>(params)...)) {
  static_assert(
      std::is_same<decltype(f(std::forward<Params>(params)...)), R>::value,
      "Return type does not match");
  decltype(f(std::forward<Params>(params)...)) result;
  do {
    result = f(std::forward<Params>(params)...);
  } while (result == r && errno == EINTR);
  return result;
}

template <typename F, typename... Params>
auto SyscallRetry(F f, Params&&... params)
    -> decltype(f(std::forward<Params>(params)...)) {
  return SyscallRetryOnError(-1, f, std::forward<Params>(params)...);
}

template <typename Runner>
class ParametrizedKernel final : public KernelInterface {
 public:
  static_assert(std::is_trivially_destructible<Runner>::value,
                "Runner is used as static, must be trivially destructible");

  ~ParametrizedKernel() override {}

  int bind(int fd, const struct sockaddr* addr, socklen_t addr_len) override {
    static Runner syscall("bind");
    return syscall.Retry(&::bind, fd, addr, addr_len);
  }
  int close(int fd) override {
    static Runner syscall("close");
    return syscall.Retry(&::close, fd);
  }
  int ioctl(int fd, int request, void* argp) override {
    static Runner syscall("ioctl");
    return syscall.Retry(&::ioctl, fd, request, argp);
  }
  int open(const char* pathname, int flags) override {
    static Runner syscall("open");
    return syscall.Retry(&::open, pathname, flags);
  }
  ssize_t read(int fd, void* buf, size_t count) override {
    static Runner syscall("read");
    return syscall.Run(&::read, fd, buf, count);
  }
  ssize_t recvfrom(int sockfd,
                   void* buf,
                   size_t len,
                   int flags,
                   struct sockaddr* src_addr,
                   socklen_t* addrlen) override {
    static Runner syscall("recvfrom");
    return syscall.RetryOnError(&::recvfrom, static_cast<ssize_t>(-1), sockfd,
                                buf, len, flags, src_addr, addrlen);
  }
  ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags) override {
    static Runner syscall("sendmsg");
    return syscall.RetryOnError(&::sendmsg, static_cast<ssize_t>(-1), sockfd,
                                msg, flags);
  }
  ssize_t sendto(int sockfd,
                 const void* buf,
                 size_t len,
                 int flags,
                 const struct sockaddr* dest_addr,
                 socklen_t addrlen) override {
    static Runner syscall("sendto");
    return syscall.RetryOnError(&::sendto, static_cast<ssize_t>(-1), sockfd,
                                buf, len, flags, dest_addr, addrlen);
  }
  int socket(int domain, int type, int protocol) override {
    static Runner syscall("socket");
    return syscall.Retry(&::socket, domain, type, protocol);
  }
  int setsockopt(int fd,
                 int level,
                 int optname,
                 const void* optval,
                 socklen_t optlen) override {
    static Runner syscall("setsockopt");
    return syscall.Retry(&::setsockopt, fd, level, optname, optval, optlen);
  }
  ssize_t write(int fd, const void* buf, size_t count) override {
    static Runner syscall("write");
    return syscall.Run(&::write, fd, buf, count);
  }
};

class DefaultKernelRunner {
 public:
  explicit DefaultKernelRunner(const char* name) {}

  template <typename F, typename R, typename... Params>
  static auto RetryOnError(F f, R r, Params&&... params)
      -> decltype(f(std::forward<Params>(params)...)) {
    return SyscallRetryOnError(r, f, std::forward<Params>(params)...);
  }

  template <typename F, typename... Params>
  static auto Retry(F f, Params&&... params)
      -> decltype(f(std::forward<Params>(params)...)) {
    return SyscallRetry(f, std::forward<Params>(params)...);
  }

  template <typename F, typename... Params>
  static auto Run(F f, Params&&... params)
      -> decltype(f(std::forward<Params>(params)...)) {
    return f(std::forward<Params>(params)...);
  }
};

using Kernel = ParametrizedKernel<DefaultKernelRunner>;

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_KERNEL_INTERFACE_H_
