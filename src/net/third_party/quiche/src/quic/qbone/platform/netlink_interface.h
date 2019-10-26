// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_NETLINK_INTERFACE_H_
#define QUICHE_QUIC_QBONE_PLATFORM_NETLINK_INTERFACE_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/qbone/platform/ip_range.h"

namespace quic {

constexpr int kHwAddrSize = 6;

class NetlinkParserInterface {
 public:
  virtual ~NetlinkParserInterface() {}
  virtual void Run(struct nlmsghdr* netlink_message) = 0;
};

// An interface providing convenience methods for manipulating IP address and
// routing table using netlink (man 7 netlink) socket.
class NetlinkInterface {
 public:
  virtual ~NetlinkInterface() = default;

  // Link information returned from GetLinkInfo.
  struct LinkInfo {
    int index;
    uint8_t type;
    uint8_t hardware_address[kHwAddrSize];
    uint8_t broadcast_address[kHwAddrSize];
    size_t hardware_address_length;   // 0 if no hardware address found
    size_t broadcast_address_length;  // 0 if no broadcast address found
  };

  // Gets the link information for the interface referred by the given
  // interface_name.
  virtual bool GetLinkInfo(const string& interface_name,
                           LinkInfo* link_info) = 0;

  // Address information reported back from GetAddresses.
  struct AddressInfo {
    QuicIpAddress local_address;
    QuicIpAddress interface_address;
    uint8_t prefix_length = 0;
    uint8_t scope = 0;
  };

  // Gets the addresses for the given interface referred by the given
  // interface_index.
  virtual bool GetAddresses(int interface_index,
                            uint8_t unwanted_flags,
                            std::vector<AddressInfo>* addresses,
                            int* num_ipv6_nodad_dadfailed_addresses) = 0;

  enum class Verb {
    kAdd,
    kRemove,
    kReplace,
  };

  // Performs the given verb that modifies local addresses on the given
  // interface_index.
  //
  // additional_attributes are RTAs (man 7 rtnelink) that will be sent together
  // with the netlink message. Note that rta_len in each RTA is used to decide
  // the length of the payload. The caller is responsible for making sure
  // payload bytes are accessible after the RTA header.
  virtual bool ChangeLocalAddress(
      uint32_t interface_index,
      Verb verb,
      const QuicIpAddress& address,
      uint8_t prefix_length,
      uint8_t ifa_flags,
      uint8_t ifa_scope,
      const std::vector<struct rtattr*>& additional_attributes) = 0;

  // Routing rule reported back from GetRouteInfo.
  struct RoutingRule {
    uint32_t table;
    IpRange destination_subnet;
    QuicIpAddress preferred_source;
    uint8_t scope;
    int out_interface;
  };

  struct IpRule {
    uint32_t table;
    IpRange source_range;
  };

  // Gets the list of routing rules from the main routing table (RT_TABLE_MAIN),
  // which is programmable.
  virtual bool GetRouteInfo(std::vector<RoutingRule>* routing_rules) = 0;

  // Performs the given Verb on the matching rule in the main routing table
  // (RT_TABLE_MAIN).
  //
  // preferred_source can be !IsInitialized(), in which case it will be omitted.
  //
  // For Verb::kRemove, rule matching is done by (destination_subnet, scope,
  // preferred_source, interface_index). Return true if a matching rule is
  // found. interface_index can be 0 for wilecard.
  //
  // For Verb::kAdd, rule matching is done by destination_subnet. If a rule for
  // the given destination_subnet already exists, nothing will happen and false
  // is returned.
  //
  // For Verb::kReplace, rule matching is done by destination_subnet. If no
  // matching rule is found, a new entry will be created.
  virtual bool ChangeRoute(Verb verb,
                           uint32_t table,
                           const IpRange& destination_subnet,
                           uint8_t scope,
                           QuicIpAddress preferred_source,
                           int32_t interface_index) = 0;

  // Returns the set of all rules in the routing policy database.
  virtual bool GetRuleInfo(std::vector<IpRule>* ip_rules) = 0;

  // Performs the give verb on the matching rule in the routing policy database.
  // When deleting a rule, the |source_range| may be unspecified, in which case
  // the lowest priority rule from |table| will be removed. When adding a rule,
  // the |source_address| must be specified.
  virtual bool ChangeRule(Verb verb, uint32_t table, IpRange source_range) = 0;

  // Sends a netlink message to the kernel. iov and iovlen represents an array
  // of struct iovec to be fed into sendmsg. The caller needs to make sure the
  // message conform to what's expected by NLMSG_* macros.
  //
  // This can be useful if more flexibility is needed than the provided
  // convenient methods can provide.
  virtual bool Send(struct iovec* iov, size_t iovlen) = 0;

  // Receives a netlink message from the kernel.
  // parser will be called on the caller's stack.
  //
  // This can be useful if more flexibility is needed than the provided
  // convenient methods can provide.
  virtual bool Recv(uint32_t seq, NetlinkParserInterface* parser) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_NETLINK_INTERFACE_H_
