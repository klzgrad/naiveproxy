// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/bonnet/tun_device_packet_exchanger.h"

#include <netinet/icmp6.h>
#include <netinet/ip6.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "quiche/quic/qbone/platform/icmp_packet.h"
#include "quiche/quic/qbone/platform/netlink_interface.h"
#include "quiche/quic/qbone/qbone_constants.h"

namespace quic {

TunDevicePacketExchanger::TunDevicePacketExchanger(
    size_t mtu, KernelInterface* kernel, NetlinkInterface* netlink,
    QbonePacketExchanger::Visitor* visitor, size_t max_pending_packets,
    bool is_tap, StatsInterface* stats, absl::string_view ifname)
    : QbonePacketExchanger(visitor, max_pending_packets),
      mtu_(mtu),
      kernel_(kernel),
      netlink_(netlink),
      ifname_(ifname),
      is_tap_(is_tap),
      stats_(stats) {
  if (is_tap_) {
    mtu_ += ETH_HLEN;
  }
}

bool TunDevicePacketExchanger::WritePacket(const char* packet, size_t size,
                                           bool* blocked, std::string* error) {
  *blocked = false;
  if (fd_ < 0) {
    *error = absl::StrCat("Invalid file descriptor of the TUN device: ", fd_);
    stats_->OnWriteError(error);
    return false;
  }

  auto buffer = std::make_unique<QuicData>(packet, size);
  if (is_tap_) {
    buffer = ApplyL2Headers(*buffer);
  }
  int result = kernel_->write(fd_, buffer->data(), buffer->length());
  if (result == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      // The tunnel is blocked. Note that this does not mean the receive buffer
      // of a TCP connection is filled. This simply means the TUN device itself
      // is blocked on handing packets to the rest of the kernel.
      *error =
          absl::ErrnoToStatus(errno, "Write to the TUN device was blocked.")
              .message();
      *blocked = true;
      stats_->OnWriteError(error);
    }
    return false;
  }
  stats_->OnPacketWritten(result);

  return true;
}

std::unique_ptr<QuicData> TunDevicePacketExchanger::ReadPacket(
    bool* blocked, std::string* error) {
  *blocked = false;
  if (fd_ < 0) {
    *error = absl::StrCat("Invalid file descriptor of the TUN device: ", fd_);
    stats_->OnReadError(error);
    return nullptr;
  }
  // Reading on a TUN device returns a packet at a time. If the packet is longer
  // than the buffer, it's truncated.
  auto read_buffer = std::make_unique<char[]>(mtu_);
  int result = kernel_->read(fd_, read_buffer.get(), mtu_);
  // Note that 0 means end of file, but we're talking about a TUN device - there
  // is no end of file. Therefore 0 also indicates error.
  if (result <= 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      *error =
          absl::ErrnoToStatus(errno, "Read from the TUN device was blocked.")
              .message();
      *blocked = true;
      stats_->OnReadError(error);
    }
    return nullptr;
  }

  auto buffer = std::make_unique<QuicData>(read_buffer.release(), result, true);
  if (is_tap_) {
    buffer = ConsumeL2Headers(*buffer);
  }
  if (buffer) {
    stats_->OnPacketRead(buffer->length());
  }
  return buffer;
}

void TunDevicePacketExchanger::set_file_descriptor(int fd) { fd_ = fd; }

const TunDevicePacketExchanger::StatsInterface*
TunDevicePacketExchanger::stats_interface() const {
  return stats_;
}

std::unique_ptr<QuicData> TunDevicePacketExchanger::ApplyL2Headers(
    const QuicData& l3_packet) {
  if (is_tap_ && !mac_initialized_) {
    NetlinkInterface::LinkInfo link_info{};
    if (netlink_->GetLinkInfo(ifname_, &link_info)) {
      memcpy(tap_mac_, link_info.hardware_address, ETH_ALEN);
      mac_initialized_ = true;
    } else {
      QUIC_LOG_EVERY_N_SEC(ERROR, 30)
          << "Unable to get link info for: " << ifname_;
    }
  }

  const auto l2_packet_size = l3_packet.length() + ETH_HLEN;
  auto l2_buffer = std::make_unique<char[]>(l2_packet_size);

  // Populate the Ethernet header
  auto* hdr = reinterpret_cast<ethhdr*>(l2_buffer.get());
  // Set src & dst to my own address
  memcpy(hdr->h_dest, tap_mac_, ETH_ALEN);
  memcpy(hdr->h_source, tap_mac_, ETH_ALEN);
  // Assume ipv6 for now
  // TODO(b/195113643): Support additional protocols.
  hdr->h_proto = absl::ghtons(ETH_P_IPV6);

  // Copy the l3 packet into buffer, just after the ethernet header.
  memcpy(l2_buffer.get() + ETH_HLEN, l3_packet.data(), l3_packet.length());

  return std::make_unique<QuicData>(l2_buffer.release(), l2_packet_size, true);
}

std::unique_ptr<QuicData> TunDevicePacketExchanger::ConsumeL2Headers(
    const QuicData& l2_packet) {
  if (l2_packet.length() < ETH_HLEN) {
    // Packet is too short for ethernet headers. Drop it.
    return nullptr;
  }
  auto* hdr = reinterpret_cast<const ethhdr*>(l2_packet.data());
  if (hdr->h_proto != absl::ghtons(ETH_P_IPV6)) {
    return nullptr;
  }
  constexpr auto kIp6PrefixLen = ETH_HLEN + sizeof(ip6_hdr);
  constexpr auto kIcmp6PrefixLen = kIp6PrefixLen + sizeof(icmp6_hdr);
  if (l2_packet.length() < kIp6PrefixLen) {
    // Packet is too short to be ipv6. Drop it.
    return nullptr;
  }
  auto* ip_hdr = reinterpret_cast<const ip6_hdr*>(l2_packet.data() + ETH_HLEN);
  const bool is_icmp = ip_hdr->ip6_ctlun.ip6_un1.ip6_un1_nxt == IPPROTO_ICMPV6;

  bool is_neighbor_solicit = false;
  if (is_icmp) {
    if (l2_packet.length() < kIcmp6PrefixLen) {
      // Packet is too short to be icmp6. Drop it.
      return nullptr;
    }
    is_neighbor_solicit =
        reinterpret_cast<const icmp6_hdr*>(l2_packet.data() + kIp6PrefixLen)
            ->icmp6_type == ND_NEIGHBOR_SOLICIT;
  }

  if (is_neighbor_solicit) {
    // If we've received a neighbor solicitation, craft an advertisement to
    // respond with and write it back to the local interface.
    auto* icmp6_payload = l2_packet.data() + kIcmp6PrefixLen;

    QuicIpAddress target_address(
        *reinterpret_cast<const in6_addr*>(icmp6_payload));
    if (target_address != *QboneConstants::GatewayAddress()) {
      // Only respond to solicitations for our gateway address
      return nullptr;
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
    memcpy(payload.get(), icmp6_payload, sizeof(in6_addr));
    // Setup the Target link-layer address option:
    //      0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |     Type      |    Length     |    Link-Layer Address ...
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    int pos = sizeof(in6_addr);
    payload[pos++] = ND_OPT_TARGET_LINKADDR;    // Type
    payload[pos++] = 1;                         // Length in units of 8 octets
    memcpy(&payload[pos], tap_mac_, ETH_ALEN);  // This interfaces' MAC address

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
                       bool blocked;
                       std::string error;
                       WritePacket(packet.data(), packet.size(), &blocked,
                                   &error);
                     });
    // Do not forward the neighbor solicitation through the tunnel since it's
    // link-local.
    return nullptr;
  }

  // If this isn't a Neighbor Solicitation, remove the L2 headers and forward
  // it as though it were an L3 packet.
  const auto l3_packet_size = l2_packet.length() - ETH_HLEN;
  auto shift_buffer = std::make_unique<char[]>(l3_packet_size);
  memcpy(shift_buffer.get(), l2_packet.data() + ETH_HLEN, l3_packet_size);

  return std::make_unique<QuicData>(shift_buffer.release(), l3_packet_size,
                                    true);
}

}  // namespace quic
