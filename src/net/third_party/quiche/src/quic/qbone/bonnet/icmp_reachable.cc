// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/bonnet/icmp_reachable.h"

#include <netinet/ip6.h>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mutex.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/quic/qbone/platform/icmp_packet.h"

namespace quic {
namespace {

constexpr int kEpollFlags = EPOLLIN | EPOLLET;
constexpr size_t kMtu = 1280;

constexpr size_t kIPv6AddrSize = sizeof(in6_addr);

}  // namespace

const char kUnknownSource[] = "UNKNOWN";
const char kNoSource[] = "N/A";

IcmpReachable::IcmpReachable(QuicIpAddress source,
                             QuicIpAddress destination,
                             absl::Duration timeout,
                             KernelInterface* kernel,
                             QuicEpollServer* epoll_server,
                             StatsInterface* stats)
    : timeout_(timeout),
      cb_(this),
      kernel_(kernel),
      epoll_server_(epoll_server),
      stats_(stats),
      send_fd_(0),
      recv_fd_(0) {
  src_.sin6_family = AF_INET6;
  dst_.sin6_family = AF_INET6;

  memcpy(&src_.sin6_addr, source.ToPackedString().data(), kIPv6AddrSize);
  memcpy(&dst_.sin6_addr, destination.ToPackedString().data(), kIPv6AddrSize);
}

IcmpReachable::~IcmpReachable() {
  if (send_fd_ > 0) {
    kernel_->close(send_fd_);
  }
  if (recv_fd_ > 0) {
    if (!epoll_server_->ShutdownCalled()) {
      epoll_server_->UnregisterFD(recv_fd_);
    }

    kernel_->close(recv_fd_);
  }
}

bool IcmpReachable::Init() {
  send_fd_ = kernel_->socket(PF_INET6, SOCK_RAW | SOCK_NONBLOCK, IPPROTO_RAW);
  if (send_fd_ < 0) {
    QUIC_LOG(ERROR) << "Unable to open socket: " << errno;
    return false;
  }

  if (kernel_->bind(send_fd_, reinterpret_cast<struct sockaddr*>(&src_),
                    sizeof(sockaddr_in6)) < 0) {
    QUIC_LOG(ERROR) << "Unable to bind socket: " << errno;
    return false;
  }

  recv_fd_ =
      kernel_->socket(PF_INET6, SOCK_RAW | SOCK_NONBLOCK, IPPROTO_ICMPV6);
  if (recv_fd_ < 0) {
    QUIC_LOG(ERROR) << "Unable to open socket: " << errno;
    return false;
  }

  if (kernel_->bind(recv_fd_, reinterpret_cast<struct sockaddr*>(&src_),
                    sizeof(sockaddr_in6)) < 0) {
    QUIC_LOG(ERROR) << "Unable to bind socket: " << errno;
    return false;
  }

  icmp6_filter filter;
  ICMP6_FILTER_SETBLOCKALL(&filter);
  ICMP6_FILTER_SETPASS(ICMP6_ECHO_REPLY, &filter);
  if (kernel_->setsockopt(recv_fd_, SOL_ICMPV6, ICMP6_FILTER, &filter,
                          sizeof(filter)) < 0) {
    QUIC_LOG(ERROR) << "Unable to set ICMP6 filter.";
    return false;
  }

  epoll_server_->RegisterFD(recv_fd_, &cb_, kEpollFlags);
  epoll_server_->RegisterAlarm(0, this);

  epoll_server_->set_timeout_in_us(50000);

  QuicWriterMutexLock mu(&header_lock_);
  icmp_header_.icmp6_type = ICMP6_ECHO_REQUEST;
  icmp_header_.icmp6_code = 0;

  QuicRandom::GetInstance()->RandBytes(&icmp_header_.icmp6_id,
                                       sizeof(uint16_t));

  return true;
}

bool IcmpReachable::OnEvent(int fd) {
  char buffer[kMtu];

  sockaddr_in6 source_addr{};
  socklen_t source_addr_len = sizeof(source_addr);

  ssize_t size = kernel_->recvfrom(fd, &buffer, kMtu, 0,
                                   reinterpret_cast<sockaddr*>(&source_addr),
                                   &source_addr_len);

  if (size < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      stats_->OnReadError(errno);
    }
    return false;
  }

  QUIC_VLOG(2) << QuicTextUtils::HexDump(QuicStringPiece(buffer, size));

  auto* header = reinterpret_cast<const icmp6_hdr*>(&buffer);
  QuicWriterMutexLock mu(&header_lock_);
  if (header->icmp6_data32[0] != icmp_header_.icmp6_data32[0]) {
    QUIC_VLOG(2) << "Unexpected response. id: " << header->icmp6_id
                 << " seq: " << header->icmp6_seq
                 << " Expected id: " << icmp_header_.icmp6_id
                 << " seq: " << icmp_header_.icmp6_seq;
    return true;
  }
  end_ = absl::Now();
  QUIC_VLOG(1) << "Received ping response in "
               << absl::ToInt64Microseconds(end_ - start_) << "us.";

  string source;
  QuicIpAddress source_ip;
  if (!source_ip.FromPackedString(
          reinterpret_cast<char*>(&source_addr.sin6_addr), sizeof(in6_addr))) {
    QUIC_LOG(WARNING) << "Unable to parse source address.";
    source = kUnknownSource;
  } else {
    source = source_ip.ToString();
  }
  stats_->OnEvent({Status::REACHABLE, end_ - start_, source});
  return true;
}

int64 /* allow-non-std-int */ IcmpReachable::OnAlarm() {
  EpollAlarm::OnAlarm();

  QuicWriterMutexLock mu(&header_lock_);

  if (end_ < start_) {
    QUIC_VLOG(1) << "Timed out on sequence: " << icmp_header_.icmp6_seq;
    stats_->OnEvent({Status::UNREACHABLE, absl::ZeroDuration(), kNoSource});
  }

  icmp_header_.icmp6_seq++;
  CreateIcmpPacket(src_.sin6_addr, dst_.sin6_addr, icmp_header_, "",
                   [this](QuicStringPiece packet) {
                     QUIC_VLOG(2) << QuicTextUtils::HexDump(packet);

                     ssize_t size = kernel_->sendto(
                         send_fd_, packet.data(), packet.size(), 0,
                         reinterpret_cast<struct sockaddr*>(&dst_),
                         sizeof(sockaddr_in6));

                     if (size < packet.size()) {
                       stats_->OnWriteError(errno);
                     }
                     start_ = absl::Now();
                   });

  return absl::ToUnixMicros(absl::Now() + timeout_);
}

QuicStringPiece IcmpReachable::StatusName(IcmpReachable::Status status) {
  switch (status) {
    case REACHABLE:
      return "REACHABLE";
    case UNREACHABLE:
      return "UNREACHABLE";
    default:
      return "UNKNOWN";
  }
}

void IcmpReachable::EpollCallback::OnEvent(int fd, QuicEpollEvent* event) {
  bool can_read_more = reachable_->OnEvent(fd);
  if (can_read_more) {
    event->out_ready_mask |= EPOLLIN;
  }
}

void IcmpReachable::EpollCallback::OnShutdown(QuicEpollServer* eps, int fd) {
  eps->UnregisterFD(fd);
}

string IcmpReachable::EpollCallback::Name() const {
  return "ICMP Reachable";
}

}  // namespace quic
