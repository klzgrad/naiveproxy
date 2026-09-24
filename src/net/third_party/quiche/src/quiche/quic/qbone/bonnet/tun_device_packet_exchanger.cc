// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/bonnet/tun_device_packet_exchanger.h"

#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <sys/uio.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/qbone/bonnet/qbone_client_packet_exchanger.h"
#include "quiche/quic/qbone/platform/icmp_packet.h"
#include "quiche/quic/qbone/platform/kernel_interface.h"
#include "quiche/quic/qbone/platform/netlink_interface.h"
#include "quiche/quic/qbone/qbone_constants.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace quic {

using ::quiche::QuicheEndian;

TunDevicePacketExchanger::TunDevicePacketExchanger(
    size_t mtu, KernelInterface* kernel, NetlinkInterface* netlink,
    Visitor* absl_nonnull visitor, bool is_tap, absl::string_view ifname)
    : kernel_(kernel),
      netlink_(netlink),
      visitor_(*visitor),
      ifname_(ifname),
      // Reading on a TUN device returns a packet at a time. If the packet is
      // longer than the buffer, it's truncated.
      read_buffer_(mtu),
      is_tap_(is_tap) {}

TunDevicePacketExchanger::~TunDevicePacketExchanger() {
  QUIC_BUG_IF(bonnet_tun_device_packet_exchanger_not_stopped,
              read_fd_ >= 0 || write_fd_ >= 0);
}

void TunDevicePacketExchanger::Start(
    int read_fd, int write_fd,
    QboneClientPacketExchanger* absl_nullable exchanger) {
  if (exchanger == nullptr) {
    exchanger = this;
  }

  // Allow idempotent Start() calls with the same file descriptors, but
  // otherwise it's a bug to try starting an already started exchanger.
  QUIC_BUG_IF(qbone_tun_device_packet_exchanger_already_started,
              (read_fd_ >= 0 || write_fd_ >= 0 || exchanger_ != nullptr) &&
                  (read_fd_ != read_fd || write_fd_ != write_fd ||
                   exchanger_ != exchanger));

  QUIC_BUG_IF(qbone_tun_device_packet_exchanger_invalid_read_fd, read_fd < 0);
  QUIC_BUG_IF(qbone_tun_device_packet_exchanger_invalid_write_fd, write_fd < 0);

  read_fd_ = read_fd;
  write_fd_ = write_fd;
  exchanger_ = exchanger;
}

void TunDevicePacketExchanger::Stop() {
  // This implementation does not employ any worker threads, so there cannot be
  // any pending operations to wait for completion.
  read_fd_ = -1;
  write_fd_ = -1;
  exchanger_ = nullptr;
}

int TunDevicePacketExchanger::OnReadFromNetworkReady(int max_packets_to_read) {
  if (read_fd_ < 0) {
    QUIC_BUG(qbone_tun_device_packet_exchanger_read_with_invalid_fd)
        << "Invalid file descriptor of the TUN device: " << read_fd_;
    return 0;
  } else if (!exchanger_) {
    QUIC_BUG(qbone_tun_device_packet_exchanger_read_with_null_exchanger);
    return 0;
  }

  int packets_read = 0;
  for (int i = 0; i < max_packets_to_read; ++i) {
    // Should be at least one packet available to read if this is called, so
    // fully process any blocked errors on the first read. After that, blocked
    // errors are just the signal that there are no more packets to read.
    bool exchange_blocked_error = packets_read == 0;

    if (ReadAndExchangeSinglePacket(exchange_blocked_error)) {
      packets_read++;
    } else {
      break;
    }
  }

  return packets_read;
}

void TunDevicePacketExchanger::WritePacketToNetwork(
    absl::Span<const std::byte> packet) {
  if (write_fd_ < 0) {
    QUIC_BUG(qbone_tun_device_packet_exchanger_write_with_invalid_fd)
        << "Invalid file descriptor of the TUN device: " << write_fd_;
    return;
  }

  if (is_tap_ && !eth_hdr_initialized_) {
    InitializeEthHdr();
  }
  struct iovec iov[2];
  iov[0].iov_base = is_tap_ ? &eth_hdr_ : nullptr;
  iov[0].iov_len = is_tap_ ? ETH_HLEN : 0;
  iov[1].iov_base = const_cast<std::byte*>(packet.data());
  iov[1].iov_len = packet.size();

  absl::Status status = absl::OkStatus();
  absl::Time start = absl::Now();
  int result = kernel_->writev(write_fd_, iov, std::size(iov));
  if (result < 0) {
    status = absl::ErrnoToStatus(errno, "Write to the TUN device failed.");
  }
  absl::Duration latency = std::max(absl::Now() - start, absl::ZeroDuration());

  if (!status.ok()) {
    QUIC_LOG_EVERY_N_SEC(ERROR, 60) << "Packet write failed: " << status;
    visitor_.OnWrite(std::move(status));
    return;
  }

  visitor_.OnWrite(std::vector<WriteResult>{
      WriteResult{.packet = std::move(packet), .latency = latency}});
}

bool TunDevicePacketExchanger::ReadAndExchangeSinglePacket(
    bool exchange_blocked_error) {
  QUICHE_DCHECK_GE(read_fd_, 0);

  // TODO(ericorth): Consider allocating these buffers once and reusing rather
  // than repeating for each packet.
  ethhdr eth_header;
  struct iovec iov[2];

  iov[0].iov_base = is_tap_ ? &eth_header : nullptr;
  iov[0].iov_len = is_tap_ ? ETH_HLEN : 0;
  iov[1].iov_base = read_buffer_.data();
  iov[1].iov_len = read_buffer_.size();

  absl::Status status = absl::OkStatus();
  absl::Time start = absl::Now();
  int result = kernel_->readv(read_fd_, iov, std::size(iov));
  int saved_errno = errno;
  absl::Duration latency = std::max(absl::Now() - start, absl::ZeroDuration());

  if (result < 0) {
    if ((saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) &&
        !exchange_blocked_error) {
      // No more packets available to read.
      return false;
    } else {
      status =
          absl::ErrnoToStatus(saved_errno, "Read from the TUN device failed.");
    }
  } else if (result == 0) {
    // Note that 0 means end of file, but we're talking about a TUN device -
    // there is no end of file. Therefore 0 also indicates error.
    status = absl::InternalError(
        "Read from the TUN device returned unexpected 0 (EOF).");
  }

  if (!status.ok()) {
    QUIC_LOG_EVERY_N_SEC(ERROR, 60) << "Packet read failed: " << status;
    visitor_.OnRead(std::move(status));
    return false;  // Assume no more packets after any socket read error.
  }

  int l3_packet_size = is_tap_ ? result - ETH_HLEN : result;
  if (l3_packet_size <= 0 || l3_packet_size > read_buffer_.size()) {
    absl::Status error =
        absl::InternalError(absl::StrCat("Invalid packet size."));
    QUIC_LOG_EVERY_N_SEC(ERROR, 60) << "Packet read failed: " << error;
    visitor_.OnRead(std::move(error));
    return true;  // Invalid packet does not mean there are no more packets.
  }
  absl::Span<const std::byte> l3_packet =
      absl::MakeSpan(read_buffer_.data(), l3_packet_size);

  if (is_tap_) {
    switch (ValidateL2Headers(eth_header, l3_packet)) {
      case L2ValidationResult::kInvalid: {
        absl::Status error = absl::InvalidArgumentError("Invalid L2 headers.");
        visitor_.OnRead(std::move(error));
        return true;  // Invalid packet does not mean there are no more packets.
      }
      case L2ValidationResult::kValidLinkLocal:
        return true;
      case L2ValidationResult::kValidNormal:
        // Packet is valid and should be forwarded to the tunnel. Fall through
        // to normal processing.
        break;
    }
  }

  visitor_.OnRead(std::vector<ReadResult>{
      ReadResult{.packet = l3_packet, .latency = latency}});
  return true;
}

void TunDevicePacketExchanger::InitializeEthHdr() {
  if (!eth_hdr_initialized_) {
    NetlinkInterface::LinkInfo link_info{};
    if (netlink_->GetLinkInfo(ifname_, &link_info)) {
      // Set src & dst to my own address
      memcpy(&eth_hdr_.h_dest, link_info.hardware_address, ETH_ALEN);
      memcpy(&eth_hdr_.h_source, link_info.hardware_address, ETH_ALEN);
      // Assume ipv6 for now
      // TODO(b/195113643): Support additional protocols.
      eth_hdr_.h_proto = QuicheEndian::HostToNet16(ETH_P_IPV6);
      eth_hdr_initialized_ = true;
    } else {
      QUIC_LOG_EVERY_N_SEC(ERROR, 30)
          << "Unable to get link info for: " << ifname_;
    }
  }
}

TunDevicePacketExchanger::L2ValidationResult
TunDevicePacketExchanger::ValidateL2Headers(
    const ethhdr& eth_header, absl::Span<const std::byte> packet) {
  if (eth_header.h_proto != QuicheEndian::HostToNet16(ETH_P_IPV6)) {
    return L2ValidationResult::kInvalid;
  }
  constexpr auto kIp6PrefixLen = sizeof(ip6_hdr);
  constexpr auto kIcmp6PrefixLen = kIp6PrefixLen + sizeof(icmp6_hdr);
  if (packet.length() < kIp6PrefixLen) {
    // Packet is too short to be ipv6. Drop it.
    return L2ValidationResult::kInvalid;
  }
  auto* ip_hdr = reinterpret_cast<const ip6_hdr*>(packet.data());
  const bool is_icmp = ip_hdr->ip6_ctlun.ip6_un1.ip6_un1_nxt == IPPROTO_ICMPV6;

  bool is_neighbor_solicit = false;
  if (is_icmp) {
    if (packet.length() < kIcmp6PrefixLen) {
      // Packet is too short to be icmp6. Drop it.
      return L2ValidationResult::kInvalid;
    }
    is_neighbor_solicit =
        reinterpret_cast<const icmp6_hdr*>(packet.subspan(kIp6PrefixLen).data())
            ->icmp6_type == ND_NEIGHBOR_SOLICIT;
  }

  if (is_neighbor_solicit) {
    // We need the local interface MAC address to respond.
    if (!eth_hdr_initialized_) {
      InitializeEthHdr();
    }
    // If we've received a neighbor solicitation, craft an advertisement to
    // respond with and write it back to the local interface.
    absl::Span<const std::byte> icmp6_payload = packet.subspan(kIcmp6PrefixLen);

    if (icmp6_payload.size() < sizeof(in6_addr)) {
      // Packet is too short to contain a valid ICMPv6 payload. Drop it.
      return L2ValidationResult::kInvalid;
    }

    QuicIpAddress target_address(
        *reinterpret_cast<const in6_addr*>(icmp6_payload.data()));
    if (target_address != *QboneConstants::GatewayAddress()) {
      // Only respond to solicitations for our gateway address
      return L2ValidationResult::kValidLinkLocal;
    }

    // Neighbor Advertisement crafted per:
    // https://datatracker.ietf.org/doc/html/rfc4861#section-4.4
    //
    // Using the Target link-layer address option defined at:
    // https://datatracker.ietf.org/doc/html/rfc4861#section-4.6.1
    constexpr size_t kIcmpv6OptionSize = 8;
    const int payload_size = sizeof(in6_addr) + kIcmpv6OptionSize;
    auto payload = std::make_unique<char[]>(payload_size);
    // Place the solicited IPv6 address at the beginning of the response payload
    memcpy(payload.get(), icmp6_payload.data(), sizeof(in6_addr));
    // Setup the Target link-layer address option:
    //      0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |     Type      |    Length     |    Link-Layer Address ...
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    int pos = sizeof(in6_addr);
    payload[pos++] = ND_OPT_TARGET_LINKADDR;  // Type
    payload[pos++] = 1;                       // Length in units of 8 octets
    memcpy(&payload[pos], eth_hdr_.h_source,
           ETH_ALEN);  // This interfaces' MAC address

    // Populate the ICMPv6 header
    icmp6_hdr response_hdr{};
    response_hdr.icmp6_type = ND_NEIGHBOR_ADVERT;
    // Set the solicited bit to true
    response_hdr.icmp6_dataun.icmp6_un_data8[0] = 64;
    // Craft the full ICMPv6 packet and then ship it off to WritePacket
    // to have it frame it with L2 headers and send it back to the requesting
    // neighbor.
    CreateIcmpPacket(ip_hdr->ip6_src, ip_hdr->ip6_src, response_hdr,
                     absl::string_view(payload.get(), payload_size),
                     [this](absl::string_view packet) {
                       QUICHE_DCHECK(exchanger_);
                       if (exchanger_) {
                         exchanger_->WritePacketToNetwork(absl::MakeSpan(
                             reinterpret_cast<const std::byte*>(packet.data()),
                             packet.size()));
                       }
                     });
    return L2ValidationResult::kValidLinkLocal;
  }

  return L2ValidationResult::kValidNormal;
}

}  // namespace quic
