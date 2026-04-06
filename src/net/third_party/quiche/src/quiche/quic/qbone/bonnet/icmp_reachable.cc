// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/bonnet/icmp_reachable.h"

#include <netinet/ip6.h>

#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/qbone/platform/icmp_packet.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {
namespace {

constexpr QuicSocketEventMask kEventMask =
    kSocketEventReadable | kSocketEventWritable;
constexpr size_t kMtu = 1280;

constexpr size_t kIPv6AddrSize = sizeof(in6_addr);

}  // namespace

const char kUnknownSource[] = "UNKNOWN";
const char kNoSource[] = "N/A";

IcmpReachable::IcmpReachable(absl::string_view interface_name,
                             QuicIpAddress source, QuicIpAddress destination,
                             QuicTime::Delta timeout, KernelInterface* kernel,
                             QuicEventLoop* event_loop, StatsInterface* stats)
    : timeout_(timeout),
      event_loop_(event_loop),
      clock_(event_loop->GetClock()),
      alarm_factory_(event_loop->CreateAlarmFactory()),
      cb_(this),
      alarm_(alarm_factory_->CreateAlarm(new AlarmCallback(this))),
      kernel_(kernel),
      stats_(stats),
      sock_fd_(0) {
  src_.sin6_family = AF_INET6;
  dst_.sin6_family = AF_INET6;
  // Ensure the destination has its scope set to the QBONE TUN/TAP device.
  dst_.sin6_scope_id =
      kernel_->if_nametoindex(std::string(interface_name).c_str());

  memcpy(&src_.sin6_addr, source.ToPackedString().data(), kIPv6AddrSize);
  memcpy(&dst_.sin6_addr, destination.ToPackedString().data(), kIPv6AddrSize);
}

IcmpReachable::~IcmpReachable() {
  if (sock_fd_ > 0) {
    if (polling_registered_) {
      bool success = event_loop_->UnregisterSocket(sock_fd_);
      QUICHE_DCHECK(success);
    }
    kernel_->close(sock_fd_);
  }
}

bool IcmpReachable::Init() {
  sock_fd_ =
      kernel_->socket(PF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_ICMPV6);
  if (sock_fd_ < 0) {
    QUIC_PLOG(ERROR) << "Unable to open ICMP socket.";
    return false;
  }

  if (kernel_->bind(sock_fd_, reinterpret_cast<struct sockaddr*>(&src_),
                    sizeof(sockaddr_in6)) < 0) {
    QUIC_PLOG(ERROR) << "Unable to bind ICMP socket.";
    return false;
  }

  if (!event_loop_->RegisterSocket(sock_fd_, kEventMask, &cb_)) {
    QUIC_LOG(ERROR) << "Unable to register ICMP socket";
    return false;
  }
  polling_registered_ = true;
  alarm_->Set(clock_->Now());

  // Obtain the local port assigned to sock_fd_.
  struct sockaddr_in6 sa = {};
  socklen_t addrlen = sizeof(sa);
  if (kernel_->getsockname(sock_fd_, reinterpret_cast<struct sockaddr*>(&sa),
                           &addrlen) == -1) {
    QUIC_PLOG(ERROR) << "Unable to getsockname:";
  }

  // Per the "ping socket" kernel commit message:
  //   "ICMP headers given to send() are checked and sanitized."
  //   "The type must be ICMP_ECHO and the code must be zero"
  //   "The id is set to the number (local port) of the socket, the
  //    checksum is always recomputed."
  absl::WriterMutexLock mu(header_lock_);
  icmp_header_.icmp6_type = ICMP6_ECHO_REQUEST;
  icmp_header_.icmp6_code = 0;
  icmp_header_.icmp6_id = sa.sin6_port;
  icmp_header_.icmp6_cksum = 0;
  icmp_header_.icmp6_seq = 0;

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

  QUIC_VLOG(2) << quiche::QuicheTextUtils::HexDump(
      absl::string_view(buffer, size));

  auto* header = reinterpret_cast<const icmp6_hdr*>(&buffer);
  absl::WriterMutexLock mu(header_lock_);
  if (header->icmp6_data32[0] != icmp_header_.icmp6_data32[0]) {
    QUIC_VLOG(2) << "Unexpected response. id: " << header->icmp6_id
                 << " seq: " << header->icmp6_seq
                 << " Expected id: " << icmp_header_.icmp6_id
                 << " seq: " << icmp_header_.icmp6_seq;
    return true;
  }
  end_ = clock_->Now();
  QUIC_VLOG(1) << "Received ping response in " << (end_ - start_);

  std::string source;
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

void IcmpReachable::OnAlarm() {
  absl::WriterMutexLock mu(header_lock_);

  if (end_ < start_) {
    QUIC_VLOG(1) << "Timed out on sequence: " << icmp_header_.icmp6_seq;
    stats_->OnEvent({Status::UNREACHABLE, QuicTime::Delta::Zero(), kNoSource});
  }
  icmp_header_.icmp6_seq++;

  QUIC_VLOG(2) << "ICMP Header: "
               << quiche::QuicheTextUtils::HexDump(absl::string_view(
                      reinterpret_cast<const char*>(&icmp_header_),
                      sizeof(icmp_header_)));

  ssize_t size = kernel_->sendto(sock_fd_, &icmp_header_, sizeof(icmp6_hdr), 0,
                                 reinterpret_cast<struct sockaddr*>(&dst_),
                                 sizeof(sockaddr_in6));

  if (size < static_cast<ssize_t>(sizeof(icmp6_hdr))) {
    stats_->OnWriteError(errno);
    QUIC_PLOG(ERROR) << "Unable to send ICMP echo request:";
  }
  start_ = clock_->Now();

  alarm_->Set(clock_->ApproximateNow() + timeout_);
}

absl::string_view IcmpReachable::StatusName(IcmpReachable::Status status) {
  switch (status) {
    case REACHABLE:
      return "REACHABLE";
    case UNREACHABLE:
      return "UNREACHABLE";
    default:
      return "UNKNOWN";
  }
}

void IcmpReachable::EpollCallback::OnSocketEvent(QuicEventLoop* event_loop,
                                                 SocketFd fd,
                                                 QuicSocketEventMask events) {
  bool can_read_more = reachable_->OnEvent(fd);
  if (can_read_more) {
    bool success =
        event_loop->ArtificiallyNotifyEvent(fd, kSocketEventReadable);
    QUICHE_DCHECK(success);
  }
}

}  // namespace quic
