/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/base/unix_socket.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "perfetto/base/compiler.h"
#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/string_utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
// The include order matters on these three Windows header groups.
#include <Windows.h>

#include <WS2tcpip.h>
#include <WinSock2.h>

#include <afunix.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#include <sys/ucred.h>
#endif

#include <algorithm>
#include <memory>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
// Use a local stripped copy of vm_sockets.h from UAPI.
#include "src/base/vm_sockets.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
#include <sys/time.h>
#endif

namespace perfetto {
namespace base {

// The CMSG_* macros use NULL instead of nullptr.
// Note: MSVC doesn't have #pragma GCC diagnostic, hence the if __GNUC__.
#if defined(__GNUC__) && !PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif

namespace {

// Android takes an int instead of socklen_t for the control buffer size.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
using CBufLenType = size_t;
#else
using CBufLenType = socklen_t;
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
constexpr char kVsockNamePrefix[] = "vsock://";
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
bool IsVirtualized() {
  static bool is_virtualized = [] {
    return base::GetAndroidProp("ro.traced.hypervisor") == "true";
  }();
  return is_virtualized;
}
#endif

// A wrapper around variable-size sockaddr structs.
// This is solving the following problem: when calling connect() or bind(), the
// caller needs to take care to allocate the right struct (sockaddr_un for
// AF_UNIX, sockaddr_in for AF_INET).   Those structs have different sizes and,
// more importantly, are bigger than the base struct sockaddr.
struct SockaddrAny {
  SockaddrAny() : size() {}
  SockaddrAny(const void* addr, socklen_t sz)
      : data(new char[static_cast<size_t>(sz)]), size(sz) {
    memcpy(data.get(), addr, static_cast<size_t>(size));
  }

  const struct sockaddr* addr() const {
    return reinterpret_cast<const struct sockaddr*>(data.get());
  }

  std::unique_ptr<char[]> data;
  socklen_t size;
};

inline int MkSockFamily(SockFamily family) {
  switch (family) {
    case SockFamily::kUnix:
      return AF_UNIX;
    case SockFamily::kInet:
      return AF_INET;
    case SockFamily::kInet6:
      return AF_INET6;
    case SockFamily::kVsock:
#ifdef AF_VSOCK
      return AF_VSOCK;
#else
      return AF_UNSPEC;  // Return AF_UNSPEC on unsupported platforms.
#endif
    case SockFamily::kUnspec:
      return AF_UNSPEC;
  }
  PERFETTO_CHECK(false);  // For GCC.
}

inline int MkSockType(SockType type) {
#if defined(SOCK_CLOEXEC)
  constexpr int kSockCloExec = SOCK_CLOEXEC;
#else
  constexpr int kSockCloExec = 0;
#endif
  switch (type) {
    case SockType::kStream:
      return SOCK_STREAM | kSockCloExec;
    case SockType::kDgram:
      return SOCK_DGRAM | kSockCloExec;
    case SockType::kSeqPacket:
      return SOCK_SEQPACKET | kSockCloExec;
  }
  PERFETTO_CHECK(false);  // For GCC.
}

SockaddrAny MakeSockAddr(SockFamily family, const std::string& socket_name) {
  switch (family) {
    case SockFamily::kUnix: {
      struct sockaddr_un saddr{};
      const size_t name_len = socket_name.size();
      if (name_len + 1 /* for trailing \0 */ >= sizeof(saddr.sun_path)) {
        errno = ENAMETOOLONG;
        return SockaddrAny();
      }
      memcpy(saddr.sun_path, socket_name.data(), name_len);
      if (saddr.sun_path[0] == '@') {
        saddr.sun_path[0] = '\0';
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
        // The MSDN blog claims that abstract (non-filesystem based) AF_UNIX
        // socket are supported, but that doesn't seem true.
        PERFETTO_ELOG(
            "Abstract AF_UNIX sockets are not supported on Windows, see "
            "https://github.com/microsoft/WSL/issues/4240");
        return SockaddrAny();
#endif
      }
      saddr.sun_family = AF_UNIX;
      auto size = static_cast<socklen_t>(
          __builtin_offsetof(sockaddr_un, sun_path) + name_len + 1);

      // Abstract sockets do NOT require a trailing null terminator (which is
      // instead mandatory for filesystem sockets). Any byte up to `size`,
      // including '\0' will become part of the socket name.
      if (saddr.sun_path[0] == '\0')
        --size;
      PERFETTO_CHECK(static_cast<size_t>(size) <= sizeof(saddr));
      return SockaddrAny(&saddr, size);
    }
    case SockFamily::kInet: {
      auto parts = SplitString(socket_name, ":");
      PERFETTO_CHECK(parts.size() == 2);
      struct addrinfo* addr_info = nullptr;
      struct addrinfo hints{};
      hints.ai_family = AF_INET;
      PERFETTO_CHECK(getaddrinfo(parts[0].c_str(), parts[1].c_str(), &hints,
                                 &addr_info) == 0);
      PERFETTO_CHECK(addr_info->ai_family == AF_INET);
      SockaddrAny res(addr_info->ai_addr,
                      static_cast<socklen_t>(addr_info->ai_addrlen));
      freeaddrinfo(addr_info);
      return res;
    }
    case SockFamily::kInet6: {
      auto parts = SplitString(socket_name, "]");
      PERFETTO_CHECK(parts.size() == 2);
      auto address = SplitString(parts[0], "[");
      PERFETTO_CHECK(address.size() == 1);
      auto port = SplitString(parts[1], ":");
      PERFETTO_CHECK(port.size() == 1);
      struct addrinfo* addr_info = nullptr;
      struct addrinfo hints{};
      hints.ai_family = AF_INET6;
      PERFETTO_CHECK(getaddrinfo(address[0].c_str(), port[0].c_str(), &hints,
                                 &addr_info) == 0);
      PERFETTO_CHECK(addr_info->ai_family == AF_INET6);
      SockaddrAny res(addr_info->ai_addr,
                      static_cast<socklen_t>(addr_info->ai_addrlen));
      freeaddrinfo(addr_info);
      return res;
    }
    case SockFamily::kVsock: {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
      PERFETTO_CHECK(StartsWith(socket_name, kVsockNamePrefix));
      auto address_port = StripPrefix(socket_name, kVsockNamePrefix);
      auto parts = SplitString(address_port, ":");
      PERFETTO_CHECK(parts.size() == 2);
      sockaddr_vm addr;
      memset(&addr, 0, sizeof(addr));
      addr.svm_family = AF_VSOCK;
      addr.svm_cid = *base::StringToUInt32(parts[0]);
      addr.svm_port = *base::StringToUInt32(parts[1]);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
      if (IsVirtualized()) {
        // VM-to-VM VSOCK communication requires messages to be
        // routed through the host.
        addr.svm_flags = VMADDR_FLAG_TO_HOST;
      }
#endif
      SockaddrAny res(&addr, sizeof(addr));
      return res;
#else
      errno = ENOTSOCK;
      return SockaddrAny();
#endif
    }
    case SockFamily::kUnspec:
      errno = ENOTSOCK;
      return SockaddrAny();
  }
  PERFETTO_CHECK(false);  // For GCC.
}

void InitWinSockOnce() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  static bool init_winsock_once = [] {
    WSADATA ignored{};
    return WSAStartup(MAKEWORD(2, 2), &ignored) == 0;
  }();
  PERFETTO_CHECK(init_winsock_once);
#endif
}

ScopedSocketHandle CreateSocketHandle(SockFamily family, SockType type) {
  InitWinSockOnce();
  return ScopedSocketHandle(socket(MkSockFamily(family), MkSockType(type), 0));
}

std::string AddrinfoToIpStr(const struct addrinfo* addrinfo_ptr) {
  PERFETTO_CHECK(addrinfo_ptr && addrinfo_ptr->ai_addr);
  PERFETTO_CHECK((addrinfo_ptr->ai_family == AF_INET) ||
                 (addrinfo_ptr->ai_family == AF_INET6));

  void* addr_ptr = nullptr;
  char ip_str_buffer[INET6_ADDRSTRLEN];

  if (addrinfo_ptr->ai_family == AF_INET) {  // IPv4
    struct sockaddr_in* ipv4_addr =
        reinterpret_cast<struct sockaddr_in*>(addrinfo_ptr->ai_addr);
    addr_ptr = &(ipv4_addr->sin_addr);
  } else if (addrinfo_ptr->ai_family == AF_INET6) {  // IPv6
    struct sockaddr_in6* ipv6_addr =
        reinterpret_cast<struct sockaddr_in6*>(addrinfo_ptr->ai_addr);
    addr_ptr = &(ipv6_addr->sin6_addr);
  }
  PERFETTO_CHECK(inet_ntop(addrinfo_ptr->ai_family, addr_ptr, ip_str_buffer,
                           sizeof(ip_str_buffer)) != NULL);
  return std::string(ip_str_buffer);
}

}  // namespace

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
int CloseSocket(SocketHandle s) {
  return ::closesocket(s);
}
#endif

SockFamily GetSockFamily(const char* addr) {
  if (strlen(addr) == 0)
    return SockFamily::kUnspec;

  if (addr[0] == '@')
    return SockFamily::kUnix;  // Abstract AF_UNIX sockets.

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // Vsock address starts with vsock://.
  if (strncmp(addr, kVsockNamePrefix, strlen(kVsockNamePrefix)) == 0)
    return SockFamily::kVsock;
#endif

  // If `addr` ends in :NNNN it's either a kInet or kInet6 socket.
  const char* col = strrchr(addr, ':');
  if (col && CStringToInt32(col + 1).has_value()) {
    return addr[0] == '[' ? SockFamily::kInet6 : SockFamily::kInet;
  }

  return SockFamily::kUnix;  // For anything else assume it's a linked AF_UNIX.
}

std::vector<NetAddrInfo> GetNetAddrInfo(const std::string& ip,
                                        const std::string& port) {
  InitWinSockOnce();
  struct addrinfo hints;
  struct addrinfo* serv_info;
  struct addrinfo* p;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  PERFETTO_CHECK(getaddrinfo(ip.c_str(), port.c_str(), &hints, &serv_info) ==
                 0);
  std::vector<NetAddrInfo> res;
  for (p = serv_info; p != nullptr; p = p->ai_next) {
    if (p->ai_family == AF_INET) {
      std::string ip_str = AddrinfoToIpStr(p);
      std::string ip_port = ip_str + ":" + port;
      res.emplace_back(ip_port, SockFamily::kInet, SockType::kStream);
    } else if (p->ai_family == AF_INET6) {
      std::string ip_str = AddrinfoToIpStr(p);
      std::string ip_port = "[" + ip_str + "]:" + port;
      res.emplace_back(ip_port, SockFamily::kInet6, SockType::kStream);
    }
  }
  freeaddrinfo(serv_info);
  return res;
}

// +-----------------------+
// | UnixSocketRaw methods |
// +-----------------------+

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
// static
void UnixSocketRaw::ShiftMsgHdrPosix(size_t n, struct msghdr* msg) {
  using LenType = decltype(msg->msg_iovlen);  // Mac and Linux don't agree.
  for (LenType i = 0; i < msg->msg_iovlen; ++i) {
    struct iovec* vec = &msg->msg_iov[i];
    if (n < vec->iov_len) {
      // We sent a part of this iovec.
      vec->iov_base = reinterpret_cast<char*>(vec->iov_base) + n;
      vec->iov_len -= n;
      msg->msg_iov = vec;
      msg->msg_iovlen -= i;
      return;
    }
    // We sent the whole iovec.
    n -= vec->iov_len;
  }
  // We sent all the iovecs.
  PERFETTO_CHECK(n == 0);
  msg->msg_iovlen = 0;
  msg->msg_iov = nullptr;
}

// static
std::pair<UnixSocketRaw, UnixSocketRaw> UnixSocketRaw::CreatePairPosix(
    SockFamily family,
    SockType type) {
  int fds[2];
  if (socketpair(MkSockFamily(family), MkSockType(type), 0, fds) != 0) {
    return std::make_pair(UnixSocketRaw(), UnixSocketRaw());
  }
  return std::make_pair(UnixSocketRaw(ScopedFile(fds[0]), family, type),
                        UnixSocketRaw(ScopedFile(fds[1]), family, type));
}
#endif

// static
UnixSocketRaw UnixSocketRaw::CreateMayFail(SockFamily family, SockType type) {
  auto fd = CreateSocketHandle(family, type);
  if (!fd)
    return UnixSocketRaw();
  return UnixSocketRaw(std::move(fd), family, type);
}

UnixSocketRaw::UnixSocketRaw() = default;

UnixSocketRaw::UnixSocketRaw(SockFamily family, SockType type)
    : UnixSocketRaw(CreateSocketHandle(family, type), family, type) {}

UnixSocketRaw::UnixSocketRaw(ScopedSocketHandle fd,
                             SockFamily family,
                             SockType type)
    : fd_(std::move(fd)), family_(family), type_(type) {
  PERFETTO_CHECK(fd_);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  const int no_sigpipe = 1;
  setsockopt(*fd_, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif

// QNX doesn't support setting SO_REUSEADDR option when using vsocks.
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
  if (family == SockFamily::kVsock) {
    int flag = 1;
    // The reinterpret_cast<const char*> is needed for Windows, where the 4th
    // arg is a const char* (on other POSIX system is a const void*).
    PERFETTO_CHECK(!setsockopt(*fd_, SOL_SOCKET, SO_REUSEADDR,
                               reinterpret_cast<const char*>(&flag),
                               sizeof(flag)));
  }
#endif

  if (family == SockFamily::kInet || family == SockFamily::kInet6) {
    int flag = 1;
    // The reinterpret_cast<const char*> is needed for Windows, where the 4th
    // arg is a const char* (on other POSIX system is a const void*).
    PERFETTO_CHECK(!setsockopt(*fd_, SOL_SOCKET, SO_REUSEADDR,
                               reinterpret_cast<const char*>(&flag),
                               sizeof(flag)));
    // Disable Nagle's algorithm, optimize for low-latency.
    // See https://github.com/google/perfetto/issues/70.
    setsockopt(*fd_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));
  }

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // We use one event handle for all socket events, to stay consistent to what
  // we do on UNIX with the base::TaskRunner's poll().
  event_handle_.reset(WSACreateEvent());
  PERFETTO_CHECK(event_handle_);
#else
  // There is no reason why a socket should outlive the process in case of
  // exec() by default, this is just working around a broken unix design.
  SetRetainOnExec(false);
#endif
}

void UnixSocketRaw::SetBlocking(bool is_blocking) {
  PERFETTO_DCHECK(fd_);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  unsigned long flag = is_blocking ? 0 : 1;  // FIONBIO has reverse logic.
  if (is_blocking) {
    // When switching between non-blocking -> blocking mode, we need to reset
    // the event handle registration, otherwise the call will fail.
    PERFETTO_CHECK(WSAEventSelect(*fd_, *event_handle_, 0) == 0);
  }
  PERFETTO_CHECK(ioctlsocket(*fd_, static_cast<long>(FIONBIO), &flag) == 0);
  if (!is_blocking) {
    PERFETTO_CHECK(
        WSAEventSelect(*fd_, *event_handle_,
                       FD_ACCEPT | FD_CONNECT | FD_READ | FD_CLOSE) == 0);
  }
#else
  int flags = fcntl(*fd_, F_GETFL, 0);
  if (!is_blocking) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~static_cast<int>(O_NONBLOCK);
  }
  int fcntl_res = fcntl(*fd_, F_SETFL, flags);
  PERFETTO_CHECK(fcntl_res == 0);
#endif
}

void UnixSocketRaw::SetRetainOnExec(bool retain) {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
  PERFETTO_DCHECK(fd_);
  int flags = fcntl(*fd_, F_GETFD, 0);
  if (retain) {
    flags &= ~static_cast<int>(FD_CLOEXEC);
  } else {
    flags |= FD_CLOEXEC;
  }
  int fcntl_res = fcntl(*fd_, F_SETFD, flags);
  PERFETTO_CHECK(fcntl_res == 0);
#else
  ignore_result(retain);
#endif
}

void UnixSocketRaw::DcheckIsBlocking(bool expected) const {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  ignore_result(expected);
#else
  PERFETTO_DCHECK(fd_);
  bool is_blocking = (fcntl(*fd_, F_GETFL, 0) & O_NONBLOCK) == 0;
  PERFETTO_DCHECK(is_blocking == expected);
#endif
}

bool UnixSocketRaw::Bind(const std::string& socket_name) {
  PERFETTO_DCHECK(fd_);
  SockaddrAny addr = MakeSockAddr(family_, socket_name);
  if (addr.size == 0)
    return false;

  if (bind(*fd_, addr.addr(), addr.size)) {
    PERFETTO_DPLOG("bind(%s)", socket_name.c_str());
    return false;
  }

  return true;
}

bool UnixSocketRaw::Listen() {
  PERFETTO_DCHECK(fd_);
  PERFETTO_DCHECK(type_ == SockType::kStream || type_ == SockType::kSeqPacket);
  return listen(*fd_, SOMAXCONN) == 0;
}

bool UnixSocketRaw::Connect(const std::string& socket_name) {
  PERFETTO_DCHECK(fd_);
  SockaddrAny addr = MakeSockAddr(family_, socket_name);
  if (addr.size == 0)
    return false;

  int res = PERFETTO_EINTR(connect(*fd_, addr.addr(), addr.size));
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  bool continue_async = WSAGetLastError() == WSAEWOULDBLOCK;
#else
  bool continue_async = errno == EINPROGRESS;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
  // QNX doesn't support the SO_ERROR socket option for vsock.
  // Therefore block the connect call by polling the socket
  // until it is writable.
  bool is_blocking_call = family_ == SockFamily::kVsock;
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // For VM-to-VM communication block until the socket is writable.
  // Not blocking leads to race condition where no error is found
  // with SO_ERROR socket option but the socket is still not writable
  // so subsequent socket calls fail.
  bool is_blocking_call = family_ == SockFamily::kVsock && IsVirtualized();
#else
  bool is_blocking_call = false;
#endif
  if (is_blocking_call && res < 0 && continue_async) {
    pollfd pfd{*fd_, POLLOUT, 0};
    if (PERFETTO_EINTR(poll(&pfd, 1 /*nfds*/, 3000 /*timeout*/)) <= 0)
      return false;
    return (pfd.revents & POLLOUT) != 0;
  }
#endif
  if (res && !continue_async)
    return false;

  return true;
}

void UnixSocketRaw::Shutdown() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // Somebody felt very strongly about the naming of this constant.
  shutdown(*fd_, SD_BOTH);
#else
  shutdown(*fd_, SHUT_RDWR);
#endif
  fd_.reset();
}

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

ssize_t UnixSocketRaw::Send(const void* msg,
                            size_t len,
                            const int* /*send_fds*/,
                            size_t num_fds) {
  PERFETTO_DCHECK(num_fds == 0);
  return sendto(*fd_, static_cast<const char*>(msg), static_cast<int>(len), 0,
                nullptr, 0);
}

ssize_t UnixSocketRaw::Receive(void* msg,
                               size_t len,
                               ScopedFile* /*fd_vec*/,
                               size_t /*max_files*/) {
  return recv(*fd_, static_cast<char*>(msg), static_cast<int>(len), 0);
}

#else
// For the interested reader, Linux kernel dive to verify this is not only a
// theoretical possibility: sock_stream_sendmsg, if sock_alloc_send_pskb returns
// NULL [1] (which it does when it gets interrupted [2]), returns early with the
// amount of bytes already sent.
//
// [1]:
// https://elixir.bootlin.com/linux/v4.18.10/source/net/unix/af_unix.c#L1872
// [2]: https://elixir.bootlin.com/linux/v4.18.10/source/net/core/sock.c#L2101
ssize_t UnixSocketRaw::SendMsgAllPosix(struct msghdr* msg) {
  // This does not make sense on non-blocking sockets.
  PERFETTO_DCHECK(fd_);

  const bool is_blocking_with_timeout =
      tx_timeout_ms_ > 0 && ((fcntl(*fd_, F_GETFL, 0) & O_NONBLOCK) == 0);
  const int64_t start_ms = GetWallTimeMs().count();

  // Waits until some space is available in the tx buffer.
  // Returns true if some buffer space is available, false if times out.
  auto poll_or_timeout = [&] {
    PERFETTO_DCHECK(is_blocking_with_timeout);
    const int64_t deadline = start_ms + tx_timeout_ms_;
    const int64_t now_ms = GetWallTimeMs().count();
    if (now_ms >= deadline)
      return false;  // Timed out
    const int timeout_ms = static_cast<int>(deadline - now_ms);
    pollfd pfd{*fd_, POLLOUT, 0};
    return PERFETTO_EINTR(poll(&pfd, 1, timeout_ms)) > 0;
  };

// We implement blocking sends that require a timeout as non-blocking + poll.
// This is because SO_SNDTIMEO doesn't work as expected (b/193234818). On linux
// we can just pass MSG_DONTWAIT to force the send to be non-blocking. On Mac,
// instead we need to flip the O_NONBLOCK flag back and forth.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  // MSG_NOSIGNAL is not supported on Mac OS X, but in that case the socket is
  // created with SO_NOSIGPIPE (See InitializeSocket()).
  int send_flags = 0;

  if (is_blocking_with_timeout)
    SetBlocking(false);

  auto reset_nonblock_on_exit = OnScopeExit([&] {
    if (is_blocking_with_timeout)
      SetBlocking(true);
  });
#else
  int send_flags = MSG_NOSIGNAL | (is_blocking_with_timeout ? MSG_DONTWAIT : 0);
#endif

  ssize_t total_sent = 0;
  while (msg->msg_iov) {
    ssize_t send_res = PERFETTO_EINTR(sendmsg(*fd_, msg, send_flags));
    if (send_res == -1 && IsAgain(errno)) {
      if (is_blocking_with_timeout && poll_or_timeout()) {
        continue;  // Tx buffer unblocked, repeat the loop.
      }
      return total_sent;
    } else if (send_res <= 0) {
      return send_res;  // An error occurred.
    } else {
      total_sent += send_res;
      ShiftMsgHdrPosix(static_cast<size_t>(send_res), msg);
      // Only send the ancillary data with the first sendmsg call.
      msg->msg_control = nullptr;
      msg->msg_controllen = 0;
    }
  }
  return total_sent;
}

ssize_t UnixSocketRaw::Send(const void* msg,
                            size_t len,
                            const int* send_fds,
                            size_t num_fds) {
  PERFETTO_DCHECK(fd_);
  msghdr msg_hdr = {};
  iovec iov = {const_cast<void*>(msg), len};
  msg_hdr.msg_iov = &iov;
  msg_hdr.msg_iovlen = 1;
  alignas(cmsghdr) char control_buf[256];

  if (num_fds > 0) {
    const auto raw_ctl_data_sz = num_fds * sizeof(int);
    const CBufLenType control_buf_len =
        static_cast<CBufLenType>(CMSG_SPACE(raw_ctl_data_sz));
    PERFETTO_CHECK(control_buf_len <= sizeof(control_buf));
    memset(control_buf, 0, sizeof(control_buf));
    msg_hdr.msg_control = control_buf;
    msg_hdr.msg_controllen = control_buf_len;  // used by CMSG_FIRSTHDR
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg_hdr);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = static_cast<CBufLenType>(CMSG_LEN(raw_ctl_data_sz));
    memcpy(CMSG_DATA(cmsg), send_fds, num_fds * sizeof(int));
    // note: if we were to send multiple cmsghdr structures, then
    // msg_hdr.msg_controllen would need to be adjusted, see "man 3 cmsg".
  }

  return SendMsgAllPosix(&msg_hdr);
}

ssize_t UnixSocketRaw::Receive(void* msg,
                               size_t len,
                               ScopedFile* fd_vec,
                               size_t max_files) {
  PERFETTO_DCHECK(fd_);
  msghdr msg_hdr = {};
  iovec iov = {msg, len};
  msg_hdr.msg_iov = &iov;
  msg_hdr.msg_iovlen = 1;
  alignas(cmsghdr) char control_buf[256];

  if (max_files > 0) {
    msg_hdr.msg_control = control_buf;
    msg_hdr.msg_controllen =
        static_cast<CBufLenType>(CMSG_SPACE(max_files * sizeof(int)));
    PERFETTO_CHECK(msg_hdr.msg_controllen <= sizeof(control_buf));
  }
  const ssize_t sz = PERFETTO_EINTR(recvmsg(*fd_, &msg_hdr, 0));
  if (sz <= 0) {
    return sz;
  }
  PERFETTO_CHECK(static_cast<size_t>(sz) <= len);

  int* fds = nullptr;
  uint32_t fds_len = 0;

  if (max_files > 0) {
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg_hdr); cmsg;
         cmsg = CMSG_NXTHDR(&msg_hdr, cmsg)) {
      const size_t payload_len = cmsg->cmsg_len - CMSG_LEN(0);
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        PERFETTO_DCHECK(payload_len % sizeof(int) == 0u);
        PERFETTO_CHECK(fds == nullptr);
        fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
        fds_len = static_cast<uint32_t>(payload_len / sizeof(int));
      }
    }
  }

  if (msg_hdr.msg_flags & MSG_TRUNC || msg_hdr.msg_flags & MSG_CTRUNC) {
    for (size_t i = 0; fds && i < fds_len; ++i)
      close(fds[i]);
    PERFETTO_ELOG(
        "Socket message truncated. This might be due to a SELinux denial on "
        "fd:use.");
    errno = EMSGSIZE;
    return -1;
  }

  for (size_t i = 0; fds && i < fds_len; ++i) {
    if (i < max_files)
      fd_vec[i].reset(fds[i]);
    else
      close(fds[i]);
  }

  return sz;
}
#endif  // OS_WIN

bool UnixSocketRaw::SetTxTimeout(uint32_t timeout_ms) {
  PERFETTO_DCHECK(fd_);
  // On Unix-based systems, SO_SNDTIMEO isn't used for Send() because it's
  // unreliable (b/193234818). Instead we use non-blocking sendmsg() + poll().
  // See SendMsgAllPosix(). We still make the setsockopt call because
  // SO_SNDTIMEO also affects connect().
  tx_timeout_ms_ = timeout_ms;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  DWORD timeout = timeout_ms;
  ignore_result(tx_timeout_ms_);
#else
  struct timeval timeout{};
  uint32_t timeout_sec = timeout_ms / 1000;
  timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(timeout_sec);
  timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(
      (timeout_ms - (timeout_sec * 1000)) * 1000);
#endif
#if PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
  if (family() == SockFamily::kVsock) {
    // QNX doesn't support SO_SNDTIMEO for vsocks.
    return true;
  }
#endif

  return setsockopt(*fd_, SOL_SOCKET, SO_SNDTIMEO,
                    reinterpret_cast<const char*>(&timeout),
                    sizeof(timeout)) == 0;
}

bool UnixSocketRaw::SetRxTimeout(uint32_t timeout_ms) {
  PERFETTO_DCHECK(fd_);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  DWORD timeout = timeout_ms;
#else
  struct timeval timeout{};
  uint32_t timeout_sec = timeout_ms / 1000;
  timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(timeout_sec);
  timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(
      (timeout_ms - (timeout_sec * 1000)) * 1000);
#endif
  return setsockopt(*fd_, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char*>(&timeout),
                    sizeof(timeout)) == 0;
}

std::string UnixSocketRaw::GetSockAddr() const {
  struct sockaddr_storage stg{};
  socklen_t slen = sizeof(stg);
  PERFETTO_CHECK(
      getsockname(*fd_, reinterpret_cast<struct sockaddr*>(&stg), &slen) == 0);
  char addr[255]{};

  if (stg.ss_family == AF_UNIX) {
    auto* saddr = reinterpret_cast<struct sockaddr_un*>(&stg);
    static_assert(sizeof(addr) >= sizeof(saddr->sun_path), "addr too small");
    memcpy(addr, saddr->sun_path, sizeof(saddr->sun_path));
    addr[0] = addr[0] == '\0' ? '@' : addr[0];
    addr[sizeof(saddr->sun_path) - 1] = '\0';
    return std::string(addr);
  }

  if (stg.ss_family == AF_INET) {
    auto* saddr = reinterpret_cast<struct sockaddr_in*>(&stg);
    PERFETTO_CHECK(inet_ntop(AF_INET, &saddr->sin_addr, addr, sizeof(addr)));
    uint16_t port = ntohs(saddr->sin_port);
    base::StackString<255> addr_and_port("%s:%" PRIu16, addr, port);
    return addr_and_port.ToStdString();
  }

  if (stg.ss_family == AF_INET6) {
    auto* saddr = reinterpret_cast<struct sockaddr_in6*>(&stg);
    PERFETTO_CHECK(inet_ntop(AF_INET6, &saddr->sin6_addr, addr, sizeof(addr)));
    auto port = ntohs(saddr->sin6_port);
    base::StackString<255> addr_and_port("[%s]:%" PRIu16, addr, port);
    return addr_and_port.ToStdString();
  }

#if defined(AF_VSOCK) && (PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
                          PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID))
  if (stg.ss_family == AF_VSOCK) {
    auto* saddr = reinterpret_cast<struct sockaddr_vm*>(&stg);
    base::StackString<255> addr_and_port("%s%u:%u", kVsockNamePrefix,
                                         saddr->svm_cid, saddr->svm_port);
    return addr_and_port.ToStdString();
  }
#endif

  PERFETTO_FATAL("GetSockAddr() unsupported on family %d", stg.ss_family);
}

#if defined(__GNUC__) && !PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#pragma GCC diagnostic pop
#endif

// +--------------------+
// | UnixSocket methods |
// +--------------------+

// TODO(primiano): Add ThreadChecker to methods of this class.

// static
std::unique_ptr<UnixSocket> UnixSocket::Listen(const std::string& socket_name,
                                               EventListener* event_listener,
                                               TaskRunner* task_runner,
                                               SockFamily sock_family,
                                               SockType sock_type) {
  auto sock_raw = UnixSocketRaw::CreateMayFail(sock_family, sock_type);
  if (!sock_raw || !sock_raw.Bind(socket_name))
    return nullptr;

  // Forward the call to the Listen() overload below.
  return Listen(sock_raw.ReleaseFd(), event_listener, task_runner, sock_family,
                sock_type);
}

// static
std::unique_ptr<UnixSocket> UnixSocket::Listen(ScopedSocketHandle fd,
                                               EventListener* event_listener,
                                               TaskRunner* task_runner,
                                               SockFamily sock_family,
                                               SockType sock_type) {
  return std::unique_ptr<UnixSocket>(new UnixSocket(
      event_listener, task_runner, std::move(fd), State::kListening,
      sock_family, sock_type, SockPeerCredMode::kDefault));
}

// static
std::unique_ptr<UnixSocket> UnixSocket::Connect(
    const std::string& socket_name,
    EventListener* event_listener,
    TaskRunner* task_runner,
    SockFamily sock_family,
    SockType sock_type,
    SockPeerCredMode peer_cred_mode) {
  std::unique_ptr<UnixSocket> sock(new UnixSocket(
      event_listener, task_runner, sock_family, sock_type, peer_cred_mode));
  sock->DoConnect(socket_name);
  return sock;
}

// static
std::unique_ptr<UnixSocket> UnixSocket::AdoptConnected(
    ScopedSocketHandle fd,
    EventListener* event_listener,
    TaskRunner* task_runner,
    SockFamily sock_family,
    SockType sock_type,
    SockPeerCredMode peer_cred_mode) {
  return std::unique_ptr<UnixSocket>(new UnixSocket(
      event_listener, task_runner, std::move(fd), State::kConnected,
      sock_family, sock_type, peer_cred_mode));
}

UnixSocket::UnixSocket(EventListener* event_listener,
                       TaskRunner* task_runner,
                       SockFamily sock_family,
                       SockType sock_type,
                       SockPeerCredMode peer_cred_mode)
    : UnixSocket(event_listener,
                 task_runner,
                 ScopedSocketHandle(),
                 State::kDisconnected,
                 sock_family,
                 sock_type,
                 peer_cred_mode) {}

UnixSocket::UnixSocket(EventListener* event_listener,
                       TaskRunner* task_runner,
                       ScopedSocketHandle adopt_fd,
                       State adopt_state,
                       SockFamily sock_family,
                       SockType sock_type,
                       SockPeerCredMode peer_cred_mode)
    : peer_cred_mode_(peer_cred_mode),
      event_listener_(event_listener),
      task_runner_(task_runner),
      weak_ptr_factory_(this) {
  state_ = State::kDisconnected;
  if (adopt_state == State::kDisconnected) {
    PERFETTO_DCHECK(!adopt_fd);
    sock_raw_ = UnixSocketRaw::CreateMayFail(sock_family, sock_type);
    if (!sock_raw_)
      return;
  } else if (adopt_state == State::kConnected) {
    PERFETTO_DCHECK(adopt_fd);
    sock_raw_ = UnixSocketRaw(std::move(adopt_fd), sock_family, sock_type);
    state_ = State::kConnected;
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    if (peer_cred_mode_ == SockPeerCredMode::kReadOnConnect)
      ReadPeerCredentialsPosix();
#endif
  } else if (adopt_state == State::kListening) {
    // We get here from Listen().

    // |adopt_fd| might genuinely be invalid if the bind() failed.
    if (!adopt_fd)
      return;

    sock_raw_ = UnixSocketRaw(std::move(adopt_fd), sock_family, sock_type);
    if (!sock_raw_.Listen()) {
      PERFETTO_DPLOG("listen() failed");
      return;
    }
    state_ = State::kListening;
  } else {
    PERFETTO_FATAL("Unexpected adopt_state");  // Unfeasible.
  }

  PERFETTO_CHECK(sock_raw_);

  sock_raw_.SetBlocking(false);

  WeakPtr<UnixSocket> weak_ptr = weak_ptr_factory_.GetWeakPtr();

  task_runner_->AddFileDescriptorWatch(sock_raw_.watch_handle(), [weak_ptr] {
    if (weak_ptr)
      weak_ptr->OnEvent();
  });
}

UnixSocket::~UnixSocket() {
  // The implicit dtor of |weak_ptr_factory_| will no-op pending callbacks.
  Shutdown(true);
}

UnixSocketRaw UnixSocket::ReleaseSocket() {
  // This will invalidate any pending calls to OnEvent.
  state_ = State::kDisconnected;
  if (sock_raw_)
    task_runner_->RemoveFileDescriptorWatch(sock_raw_.watch_handle());

  return std::move(sock_raw_);
}

// Called only by the Connect() static constructor.
void UnixSocket::DoConnect(const std::string& socket_name) {
  PERFETTO_DCHECK(state_ == State::kDisconnected);

  // This is the only thing that can gracefully fail in the ctor.
  if (!sock_raw_)
    return NotifyConnectionState(false);

  if (!sock_raw_.Connect(socket_name))
    return NotifyConnectionState(false);

  // At this point either connect() succeeded or started asynchronously
  // (errno = EINPROGRESS).
  state_ = State::kConnecting;

  // Even if the socket is non-blocking, connecting to a UNIX socket can be
  // acknowledged straight away rather than returning EINPROGRESS.
  // The decision here is to deal with the two cases uniformly, at the cost of
  // delaying the straight-away-connect() case by one task, to avoid depending
  // on implementation details of UNIX socket on the various OSes.
  // Posting the OnEvent() below emulates a wakeup of the FD watch. OnEvent(),
  // which knows how to deal with spurious wakeups, will poll the SO_ERROR and
  // evolve, if necessary, the state into either kConnected or kDisconnected.
  WeakPtr<UnixSocket> weak_ptr = weak_ptr_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_ptr] {
    if (weak_ptr)
      weak_ptr->OnEvent();
  });
}

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
void UnixSocket::ReadPeerCredentialsPosix() {
  // Peer credentials are supported only on AF_UNIX sockets.
  if (sock_raw_.family() != SockFamily::kUnix)
    return;
  PERFETTO_CHECK(peer_cred_mode_ != SockPeerCredMode::kIgnore);

#if PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
  int fd = sock_raw_.fd();
  int res = getpeereid(fd, &peer_uid_, nullptr);
  PERFETTO_CHECK(res == 0);
  // There is no pid when obtaining peer credentials for QNX
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  struct ucred user_cred;
  socklen_t len = sizeof(user_cred);
  int fd = sock_raw_.fd();
  int res = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &user_cred, &len);
  PERFETTO_CHECK(res == 0);
  peer_uid_ = user_cred.uid;
  peer_pid_ = user_cred.pid;
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  struct xucred user_cred;
  socklen_t len = sizeof(user_cred);
  int res = getsockopt(sock_raw_.fd(), 0, LOCAL_PEERCRED, &user_cred, &len);
  PERFETTO_CHECK(res == 0 && user_cred.cr_version == XUCRED_VERSION);
  peer_uid_ = static_cast<uid_t>(user_cred.cr_uid);
  // There is no pid in the LOCAL_PEERCREDS for MacOS / FreeBSD.
#endif
}
#endif  // !OS_WIN

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
void UnixSocket::OnEvent() {
  WSANETWORKEVENTS evts{};
  PERFETTO_CHECK(WSAEnumNetworkEvents(sock_raw_.fd(), sock_raw_.watch_handle(),
                                      &evts) == 0);
  if (state_ == State::kDisconnected)
    return;  // Some spurious event, typically queued just before Shutdown().

  if (state_ == State::kConnecting && (evts.lNetworkEvents & FD_CONNECT)) {
    PERFETTO_DCHECK(sock_raw_);
    int err = evts.iErrorCode[FD_CONNECT_BIT];
    if (err) {
      PERFETTO_DPLOG("Connection error: %d", err);
      Shutdown(false);
      event_listener_->OnConnect(this, false /* connected */);
      return;
    }

    // kReadOnConnect is not supported on Windows.
    PERFETTO_DCHECK(peer_cred_mode_ != SockPeerCredMode::kReadOnConnect);
    state_ = State::kConnected;
    event_listener_->OnConnect(this, true /* connected */);
  }

  // This is deliberately NOT an else-if. When a client socket connects and
  // there is already data queued, the following will happen within the same
  // OnEvent() call:
  // 1. The block above will transition kConnecting -> kConnected.
  // 2. This block will cause an OnDataAvailable() call.
  // Unlike UNIX, where poll() keeps signalling the event until the client
  // does a recv(), Windows is more picky and stops signalling the event until
  // the next call to recv() is made. In other words, in Windows we cannot
  // miss an OnDataAvailable() call or the event pump will stop.
  if (state_ == State::kConnected) {
    if (evts.lNetworkEvents & FD_READ) {
      event_listener_->OnDataAvailable(this);
      // TODO(primiano): I am very conflicted here. Because of the behavior
      // described above, if the event listener doesn't do a Recv() call in
      // the OnDataAvailable() callback, WinSock won't notify the event ever
      // again. On one side, I don't see any reason why a client should decide
      // to not do a Recv() in OnDataAvailable. On the other side, the
      // behavior here diverges from UNIX, where OnDataAvailable() would be
      // re-posted immediately. In both cases, not doing a Recv() in
      // OnDataAvailable, leads to something bad (getting stuck on Windows,
      // getting in a hot loop on Linux), so doesn't feel we should worry too
      // much about this. If we wanted to keep the behavior consistent, here
      // we should do something like: `if (sock_raw_)
      // sock_raw_.SetBlocking(false)` (Note that the socket might be closed
      // by the time we come back here, hence the if part).
      return;
    }
    // Could read EOF and disconnect here.
    if (evts.lNetworkEvents & FD_CLOSE) {
      Shutdown(true);
      return;
    }
  }

  // New incoming connection.
  if (state_ == State::kListening && (evts.lNetworkEvents & FD_ACCEPT)) {
    // There could be more than one incoming connection behind each FD watch
    // notification. Drain'em all.
    for (;;) {
      // Note: right now we don't need the remote endpoint, hence we pass
      // nullptr to |addr| and |addrlen|. If we ever need to do so, be
      // extremely careful. Windows' WinSock API will happily write more than
      // |addrlen| (hence corrupt the stack) if the |addr| argument passed is
      // not big enough (e.g. passing a struct sockaddr_in to a AF_UNIX
      // socket, where sizeof(sockaddr_un) is >> sizef(sockaddr_in)). It seems
      // a Windows / CRT bug in the AF_UNIX implementation.
      ScopedSocketHandle new_fd(accept(sock_raw_.fd(), nullptr, nullptr));
      if (!new_fd)
        return;
      std::unique_ptr<UnixSocket> new_sock(new UnixSocket(
          event_listener_, task_runner_, std::move(new_fd), State::kConnected,
          sock_raw_.family(), sock_raw_.type(), peer_cred_mode_));
      event_listener_->OnNewIncomingConnection(this, std::move(new_sock));
    }
  }
}
#else
void UnixSocket::OnEvent() {
  if (state_ == State::kDisconnected)
    return;  // Some spurious event, typically queued just before Shutdown().

  if (state_ == State::kConnected)
    return event_listener_->OnDataAvailable(this);

  if (state_ == State::kConnecting) {
    PERFETTO_DCHECK(sock_raw_);
    int res = 0, sock_err = 0;
    bool is_error_opt_supported = true;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
    // QNX doesn't support the SO_ERROR socket option for vsock.
    // Since, we make the connect call blocking, it is fine to skip
    // the error check and simply continue with the connection flow.
    if (sock_raw_.family() == SockFamily::kVsock) {
      is_error_opt_supported = false;
    }
#endif
    if (is_error_opt_supported) {
      sock_err = EINVAL;
      socklen_t err_len = sizeof(sock_err);
      res =
          getsockopt(sock_raw_.fd(), SOL_SOCKET, SO_ERROR, &sock_err, &err_len);
    }
    if (res == 0 && sock_err == EINPROGRESS)
      return;  // Not connected yet, just a spurious FD watch wakeup.
    if (res == 0 && sock_err == 0) {
      if (peer_cred_mode_ == SockPeerCredMode::kReadOnConnect)
        ReadPeerCredentialsPosix();
      state_ = State::kConnected;
      return event_listener_->OnConnect(this, true /* connected */);
    }
    PERFETTO_DLOG("Connection error: %s", strerror(sock_err));
    Shutdown(false);
    return event_listener_->OnConnect(this, false /* connected */);
  }

  // New incoming connection.
  if (state_ == State::kListening) {
    // There could be more than one incoming connection behind each FD watch
    // notification. Drain'em all.
    for (;;) {
      ScopedFile new_fd(
          PERFETTO_EINTR(accept(sock_raw_.fd(), nullptr, nullptr)));
      if (!new_fd)
        return;
      std::unique_ptr<UnixSocket> new_sock(new UnixSocket(
          event_listener_, task_runner_, std::move(new_fd), State::kConnected,
          sock_raw_.family(), sock_raw_.type(), peer_cred_mode_));
      event_listener_->OnNewIncomingConnection(this, std::move(new_sock));
    }
  }
}
#endif

bool UnixSocket::Send(const void* msg,
                      size_t len,
                      const int* send_fds,
                      size_t num_fds) {
  if (state_ != State::kConnected) {
    errno = ENOTCONN;
    return false;
  }

  sock_raw_.SetBlocking(true);
  const ssize_t sz = sock_raw_.Send(msg, len, send_fds, num_fds);
  sock_raw_.SetBlocking(false);

  if (sz == static_cast<ssize_t>(len)) {
    return true;
  }

  // If we ever decide to support non-blocking sends again, here we should
  // watch for both EAGAIN and EWOULDBLOCK (see base::IsAgain()).

  // If sendmsg() succeeds but the returned size is >= 0 and < |len| it means
  // that the endpoint disconnected in the middle of the read, and we managed
  // to send only a portion of the buffer.
  // If sz < 0, either the other endpoint disconnected (ECONNRESET) or some
  // other error happened. In both cases we should just give up.
  PERFETTO_DPLOG("sendmsg() failed");
  Shutdown(true);
  return false;
}

void UnixSocket::Shutdown(bool notify) {
  WeakPtr<UnixSocket> weak_ptr = weak_ptr_factory_.GetWeakPtr();
  if (notify) {
    if (state_ == State::kConnected) {
      task_runner_->PostTask([weak_ptr] {
        if (weak_ptr)
          weak_ptr->event_listener_->OnDisconnect(weak_ptr.get());
      });
    } else if (state_ == State::kConnecting) {
      task_runner_->PostTask([weak_ptr] {
        if (weak_ptr)
          weak_ptr->event_listener_->OnConnect(weak_ptr.get(), false);
      });
    }
  }

  if (sock_raw_) {
    task_runner_->RemoveFileDescriptorWatch(sock_raw_.watch_handle());
    sock_raw_.Shutdown();
  }
  state_ = State::kDisconnected;
}

size_t UnixSocket::Receive(void* msg,
                           size_t len,
                           ScopedFile* fd_vec,
                           size_t max_files) {
  if (state_ != State::kConnected)
    return 0;

  const ssize_t sz = sock_raw_.Receive(msg, len, fd_vec, max_files);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  bool async_would_block = WSAGetLastError() == WSAEWOULDBLOCK;
#else
  bool async_would_block = IsAgain(errno);
#endif
  if (sz < 0 && async_would_block)
    return 0;

  if (sz <= 0) {
    Shutdown(true);
    return 0;
  }
  PERFETTO_CHECK(static_cast<size_t>(sz) <= len);
  return static_cast<size_t>(sz);
}

std::string UnixSocket::ReceiveString(size_t max_length) {
  std::unique_ptr<char[]> buf(new char[max_length + 1]);
  size_t rsize = Receive(buf.get(), max_length);
  PERFETTO_CHECK(rsize <= max_length);
  buf[rsize] = '\0';
  return std::string(buf.get());
}

void UnixSocket::NotifyConnectionState(bool success) {
  if (!success)
    Shutdown(false);

  WeakPtr<UnixSocket> weak_ptr = weak_ptr_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_ptr, success] {
    if (weak_ptr)
      weak_ptr->event_listener_->OnConnect(weak_ptr.get(), success);
  });
}

UnixSocket::EventListener::~EventListener() {}
void UnixSocket::EventListener::OnNewIncomingConnection(
    UnixSocket*,
    std::unique_ptr<UnixSocket>) {}
void UnixSocket::EventListener::OnConnect(UnixSocket*, bool) {}
void UnixSocket::EventListener::OnDisconnect(UnixSocket*) {}
void UnixSocket::EventListener::OnDataAvailable(UnixSocket*) {}

}  // namespace base
}  // namespace perfetto
