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

IcmpReachable::IcmpReachable(QuicIpAddress source, QuicIpAddress destination,
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
    bool success = event_loop_->UnregisterSocket(recv_fd_);
    QUICHE_DCHECK(success);

    kernel_->close(recv_fd_);
  }
}

bool IcmpReachable::Init() {
  send_fd_ = kernel_->socket(PF_INET6, SOCK_RAW | SOCK_NONBLOCK, IPPROTO_RAW);
  if (send_fd_ < 0) {
    QUIC_PLOG(ERROR) << "Unable to open socket.";
    return false;
  }

  if (kernel_->bind(send_fd_, reinterpret_cast<struct sockaddr*>(&src_),
                    sizeof(sockaddr_in6)) < 0) {
    QUIC_PLOG(ERROR) << "Unable to bind socket.";
    return false;
  }

  recv_fd_ =
      kernel_->socket(PF_INET6, SOCK_RAW | SOCK_NONBLOCK, IPPROTO_ICMPV6);
  if (recv_fd_ < 0) {
    QUIC_PLOG(ERROR) << "Unable to open socket.";
    return false;
  }

  if (kernel_->bind(recv_fd_, reinterpret_cast<struct sockaddr*>(&src_),
                    sizeof(sockaddr_in6)) < 0) {
    QUIC_PLOG(ERROR) << "Unable to bind socket.";
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

  if (!event_loop_->RegisterSocket(recv_fd_, kEventMask, &cb_)) {
    QUIC_LOG(ERROR) << "Unable to register recv ICMP socket";
    return false;
  }
  alarm_->Set(clock_->Now());

  quiche::QuicheWriterMutexLock mu(&header_lock_);
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

  QUIC_VLOG(2) << quiche::QuicheTextUtils::HexDump(
      absl::string_view(buffer, size));

  auto* header = reinterpret_cast<const icmp6_hdr*>(&buffer);
  quiche::QuicheWriterMutexLock mu(&header_lock_);
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
  quiche::QuicheWriterMutexLock mu(&header_lock_);

  if (end_ < start_) {
    QUIC_VLOG(1) << "Timed out on sequence: " << icmp_header_.icmp6_seq;
    stats_->OnEvent({Status::UNREACHABLE, QuicTime::Delta::Zero(), kNoSource});
  }

  icmp_header_.icmp6_seq++;
  CreateIcmpPacket(src_.sin6_addr, dst_.sin6_addr, icmp_header_, "",
                   [this](absl::string_view packet) {
                     QUIC_VLOG(2) << quiche::QuicheTextUtils::HexDump(packet);

                     ssize_t size = kernel_->sendto(
                         send_fd_, packet.data(), packet.size(), 0,
                         reinterpret_cast<struct sockaddr*>(&dst_),
                         sizeof(sockaddr_in6));

                     if (size < packet.size()) {
                       stats_->OnWriteError(errno);
                     }
                     start_ = clock_->Now();
                   });

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
