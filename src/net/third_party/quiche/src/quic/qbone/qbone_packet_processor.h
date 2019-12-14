// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_H_
#define QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_H_

#include <netinet/icmp6.h>
#include <netinet/ip6.h>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

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
    FROM_CLIENT = 0,
    // Packet is going from the network begin QBONE to the client.
    FROM_NETWORK = 1
  };

  enum class ProcessingResult {
    OK = 0,
    SILENT_DROP = 1,
    ICMP = 2,
    // Equivalent to |SILENT_DROP| at the moment, but indicates that the
    // downstream filter has buffered the packet and deferred its processing.
    // The packet may be emitted at a later time.
    DEFER = 3,
    // In addition to sending an ICMP message, also send a TCP RST. This option
    // requires the incoming packet to have been a valid TCP packet, as a TCP
    // RST requires information from the current connection state to be
    // well-formed.
    ICMP_AND_TCP_RESET = 4,
  };

  class OutputInterface {
   public:
    virtual ~OutputInterface();

    virtual void SendPacketToClient(QuicStringPiece packet) = 0;
    virtual void SendPacketToNetwork(QuicStringPiece packet) = 0;
  };

  class StatsInterface {
   public:
    virtual ~StatsInterface();

    virtual void OnPacketForwarded(Direction direction) = 0;
    virtual void OnPacketDroppedSilently(Direction direction) = 0;
    virtual void OnPacketDroppedWithIcmp(Direction direction) = 0;
    virtual void OnPacketDroppedWithTcpReset(Direction direction) = 0;
    virtual void OnPacketDeferred(Direction direction) = 0;
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
    //
    // The implementer of this method has four options to return:
    // - OK will cause the filter to pass the packet through
    // - SILENT_DROP will cause the filter to drop the packet silently
    // - ICMP will cause the filter to drop the packet and send an ICMP
    //   response.
    // - DEFER will cause the packet to be not forwarded; the filter is
    //   responsible for sending (or not sending) it later using |output|.
    //
    // Note that |output| should not be used except in the DEFER case, as the
    // processor will perform the necessary writes itself.
    virtual ProcessingResult FilterPacket(Direction direction,
                                          QuicStringPiece full_packet,
                                          QuicStringPiece payload,
                                          icmp6_hdr* icmp_header,
                                          OutputInterface* output);

   protected:
    // Helper methods that allow to easily extract information that is required
    // for filtering from the |ipv6_header| argument.  All of those assume that
    // the header is of valid size, which is true for everything passed into
    // FilterPacket().
    inline uint8_t TransportProtocolFromHeader(QuicStringPiece ipv6_header) {
      return ipv6_header[6];
    }
    inline QuicIpAddress SourceIpFromHeader(QuicStringPiece ipv6_header) {
      QuicIpAddress address;
      address.FromPackedString(&ipv6_header[8],
                               QuicIpAddress::kIPv6AddressSize);
      return address;
    }
    inline QuicIpAddress DestinationIpFromHeader(QuicStringPiece ipv6_header) {
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
  QbonePacketProcessor(QuicIpAddress self_ip,
                       QuicIpAddress client_ip,
                       size_t client_ip_subnet_length,
                       OutputInterface* output,
                       StatsInterface* stats);
  QbonePacketProcessor(const QbonePacketProcessor&) = delete;
  QbonePacketProcessor& operator=(const QbonePacketProcessor&) = delete;

  // Accepts an IPv6 packet and handles it accordingly by either forwarding it,
  // replying with an ICMP packet or silently dropping it.  |packet| will be
  // modified in the process, by having the TTL field decreased.
  void ProcessPacket(string* packet, Direction direction);

  void set_filter(std::unique_ptr<Filter> filter) {
    filter_ = std::move(filter);
  }

  void set_client_ip(QuicIpAddress client_ip) { client_ip_ = client_ip; }
  void set_client_ip_subnet_length(size_t client_ip_subnet_length) {
    client_ip_subnet_length_ = client_ip_subnet_length;
  }

  static const QuicIpAddress kInvalidIpAddress;

 protected:
  // Processes the header and returns what should be done with the packet.
  // After that, calls an external packet filter if registered.  TTL of the
  // packet may be decreased in the process.
  ProcessingResult ProcessIPv6HeaderAndFilter(string* packet,
                                              Direction direction,
                                              uint8_t* transport_protocol,
                                              char** transport_data,
                                              icmp6_hdr* icmp_header);

  void SendIcmpResponse(icmp6_hdr* icmp_header,
                        QuicStringPiece original_packet,
                        Direction original_direction);

  void SendTcpReset(QuicStringPiece original_packet,
                    Direction original_direction);

  inline bool IsValid() const { return client_ip_ != kInvalidIpAddress; }

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
  ProcessingResult ProcessIPv6Header(string* packet,
                                     Direction direction,
                                     uint8_t* transport_protocol,
                                     char** transport_data,
                                     icmp6_hdr* icmp_header);

  void SendResponse(Direction original_direction, QuicStringPiece packet);
};

}  // namespace quic
#endif  // QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_H_
