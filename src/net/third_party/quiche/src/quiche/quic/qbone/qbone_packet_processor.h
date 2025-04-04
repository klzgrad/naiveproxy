// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_H_
#define QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_H_

#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip6.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_ip_address.h"

namespace quic {

enum : size_t {
  kIPv6HeaderSize = 40,
  kICMPv6HeaderSize = sizeof(icmp6_hdr),
  kTotalICMPv6HeaderSize = kIPv6HeaderSize + kICMPv6HeaderSize,
};

// QBONE packet processor accepts packets destined in either direction
// (client-to-network or network-to-client).  It inspects them and makes
// decisions on whether they should be forwarded or dropped, replying with ICMP
// messages as appropriate.
class QbonePacketProcessor {
 public:
  enum class Direction {
    // Packet is going from the QBONE client into the network behind the QBONE.
    FROM_OFF_NETWORK = 0,
    // Packet is going from the network begin QBONE to the client.
    FROM_NETWORK = 1
  };

  enum class ProcessingResult {
    OK = 0,
    SILENT_DROP = 1,
    ICMP = 2,
    // In addition to sending an ICMP message, also send a TCP RST. This option
    // requires the incoming packet to have been a valid TCP packet, as a TCP
    // RST requires information from the current connection state to be
    // well-formed.
    ICMP_AND_TCP_RESET = 4,
    // Send a TCP RST.
    TCP_RESET = 5,
  };

  class OutputInterface {
   public:
    virtual ~OutputInterface();

    virtual void SendPacketToClient(absl::string_view packet) = 0;
    virtual void SendPacketToNetwork(absl::string_view packet) = 0;
  };

  // A visitor interface that allows the packet processor to collect stats
  // without relying on a specific backend or exposing the entire packet.
  // |traffic_class| should be extracted directly from the IPv6 header.
  class StatsInterface {
   public:
    virtual ~StatsInterface();

    virtual void OnPacketForwarded(Direction direction,
                                   uint8_t traffic_class) = 0;
    virtual void OnPacketDroppedSilently(Direction direction,
                                         uint8_t traffic_class) = 0;
    virtual void OnPacketDroppedWithIcmp(Direction direction,
                                         uint8_t traffic_class) = 0;
    virtual void OnPacketDroppedWithTcpReset(Direction direction,
                                             uint8_t traffic_class) = 0;
    virtual void RecordThroughput(size_t bytes, Direction direction,
                                  uint8_t traffic_class) = 0;
  };

  // Allows to implement a custom packet filter on top of the filtering done by
  // the packet processor itself.
  class Filter {
   public:
    virtual ~Filter();
    // The main interface function.  The following arguments are supplied:
    // - |direction|, to indicate direction of the packet.
    // - |full_packet|, which includes the IPv6 header and possibly the IPv6
    //   options that were understood by the processor.
    // - |payload|, the contents of the IPv6 packet, i.e. a TCP, a UDP or an
    //   ICMP packet.
    // - |icmp_header|, an output argument which allows the filter to specify
    //   the ICMP message with which the packet is to be rejected.
    // The method is called only on packets which were already verified as valid
    // IPv6 packets.
    virtual ProcessingResult FilterPacket(Direction direction,
                                          absl::string_view full_packet,
                                          absl::string_view payload,
                                          icmp6_hdr* icmp_header);

   protected:
    // Helper methods that allow to easily extract information that is required
    // for filtering from the |ipv6_header| argument.  All of those assume that
    // the header is of valid size, which is true for everything passed into
    // FilterPacket().
    uint8_t TransportProtocolFromHeader(absl::string_view ipv6_header) {
      return ipv6_header[6];
    }

    QuicIpAddress SourceIpFromHeader(absl::string_view ipv6_header) {
      QuicIpAddress address;
      address.FromPackedString(&ipv6_header[8],
                               QuicIpAddress::kIPv6AddressSize);
      return address;
    }
    QuicIpAddress DestinationIpFromHeader(absl::string_view ipv6_header) {
      QuicIpAddress address;
      address.FromPackedString(&ipv6_header[24],
                               QuicIpAddress::kIPv6AddressSize);
      return address;
    }
  };

  // |self_ip| is the IP address from which the processor will originate ICMP
  // messages.  |client_ip| is the expected IP address of the client, used for
  // packet validation.
  //
  // |output| and |stats| are the visitor interfaces used by the processor.
  // |output| gets notified whenever the processor decides to send a packet, and
  // |stats| gets notified about any decisions that processor makes, without a
  // reference to which packet that decision was made about.
  QbonePacketProcessor(QuicIpAddress self_ip, QuicIpAddress client_ip,
                       size_t client_ip_subnet_length, OutputInterface* output,
                       StatsInterface* stats);
  QbonePacketProcessor(const QbonePacketProcessor&) = delete;
  QbonePacketProcessor& operator=(const QbonePacketProcessor&) = delete;

  // Accepts an IPv6 packet and handles it accordingly by either forwarding it,
  // replying with an ICMP packet or silently dropping it.  |packet| will be
  // modified in the process, by having the TTL field decreased.
  void ProcessPacket(std::string* packet, Direction direction);

  void set_filter(std::unique_ptr<Filter> filter) {
    filter_ = std::move(filter);
  }

  void set_client_ip(QuicIpAddress client_ip) { client_ip_ = client_ip; }
  void set_client_ip_subnet_length(size_t client_ip_subnet_length) {
    client_ip_subnet_length_ = client_ip_subnet_length;
  }

  static const QuicIpAddress kInvalidIpAddress;

  // This function assumes that the packet is valid.
  static uint8_t TrafficClassFromHeader(absl::string_view ipv6_header);

 protected:
  // Processes the header and returns what should be done with the packet.
  // After that, calls an external packet filter if registered.  TTL of the
  // packet may be decreased in the process.
  ProcessingResult ProcessIPv6HeaderAndFilter(std::string* packet,
                                              Direction direction,
                                              uint8_t* transport_protocol,
                                              char** transport_data,
                                              icmp6_hdr* icmp_header);

  void SendIcmpResponse(in6_addr dst, icmp6_hdr* icmp_header,
                        absl::string_view payload,
                        Direction original_direction);

  void SendTcpReset(absl::string_view original_packet,
                    Direction original_direction);

  bool IsValid() const { return client_ip_ != kInvalidIpAddress; }

  // IP address of the server.  Used to send ICMP messages.
  in6_addr self_ip_;
  // IP address range of the VPN client.
  QuicIpAddress client_ip_;
  size_t client_ip_subnet_length_;

  OutputInterface* output_;
  StatsInterface* stats_;
  std::unique_ptr<Filter> filter_;

 private:
  // Performs basic sanity and permission checks on the packet, and decreases
  // the TTL.
  ProcessingResult ProcessIPv6Header(std::string* packet, Direction direction,
                                     uint8_t* transport_protocol,
                                     char** transport_data,
                                     icmp6_hdr* icmp_header);

  void SendResponse(Direction original_direction, absl::string_view packet);

  in6_addr GetDestinationFromPacket(absl::string_view packet);
};

}  // namespace quic
#endif  // QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_H_
