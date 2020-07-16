// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_NETLINK_H_
#define QUICHE_QUIC_QBONE_PLATFORM_NETLINK_H_

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/qbone/platform/ip_range.h"
#include "net/third_party/quiche/src/quic/qbone/platform/kernel_interface.h"
#include "net/third_party/quiche/src/quic/qbone/platform/netlink_interface.h"

namespace quic {

// A wrapper class to provide convenient methods of manipulating IP address and
// routing table using netlink (man 7 netlink) socket. More specifically,
// rtnetlink is used (man 7 rtnetlink).
//
// This class is not thread safe, but thread compatible, as long as callers can
// make sure Send and Recv pairs are executed in sequence for a particular
// query.
class Netlink : public NetlinkInterface {
 public:
  explicit Netlink(KernelInterface* kernel);
  ~Netlink() override;

  // Gets the link information for the interface referred by the given
  // interface_name.
  //
  // This is a synchronous communication. That should not be a problem since the
  // kernel should answer immediately.
  bool GetLinkInfo(const std::string& interface_name,
                   LinkInfo* link_info) override;

  // Gets the addresses for the given interface referred by the given
  // interface_index.
  //
  // This is a synchronous communication. This should not be a problem since the
  // kernel should answer immediately.
  bool GetAddresses(int interface_index,
                    uint8_t unwanted_flags,
                    std::vector<AddressInfo>* addresses,
                    int* num_ipv6_nodad_dadfailed_addresses) override;

  // Performs the given verb that modifies local addresses on the given
  // interface_index.
  //
  // additional_attributes are RTAs (man 7 rtnelink) that will be sent together
  // with the netlink message. Note that rta_len in each RTA is used to decide
  // the length of the payload. The caller is responsible for making sure
  // payload bytes are accessible after the RTA header.
  bool ChangeLocalAddress(
      uint32_t interface_index,
      Verb verb,
      const QuicIpAddress& address,
      uint8_t prefix_length,
      uint8_t ifa_flags,
      uint8_t ifa_scope,
      const std::vector<struct rtattr*>& additional_attributes) override;

  // Gets the list of routing rules from the main routing table (RT_TABLE_MAIN),
  // which is programmable.
  //
  // This is a synchronous communication. This should not be a problem since the
  // kernel should answer immediately.
  bool GetRouteInfo(std::vector<RoutingRule>* routing_rules) override;

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
  bool ChangeRoute(Netlink::Verb verb,
                   uint32_t table,
                   const IpRange& destination_subnet,
                   uint8_t scope,
                   QuicIpAddress preferred_source,
                   int32_t interface_index) override;

  // Returns the set of all rules in the routing policy database.
  bool GetRuleInfo(std::vector<Netlink::IpRule>* ip_rules) override;

  // Performs the give verb on the matching rule in the routing policy database.
  // When deleting a rule, the |source_range| may be unspecified, in which case
  // the lowest priority rule from |table| will be removed. When adding a rule,
  // the |source_address| must be specified.
  bool ChangeRule(Verb verb, uint32_t table, IpRange source_range) override;

  // Sends a netlink message to the kernel. iov and iovlen represents an array
  // of struct iovec to be fed into sendmsg. The caller needs to make sure the
  // message conform to what's expected by NLMSG_* macros.
  //
  // This can be useful if more flexibility is needed than the provided
  // convenient methods can provide.
  bool Send(struct iovec* iov, size_t iovlen) override;

  // Receives a netlink message from the kernel.
  // parser will be called on the caller's stack.
  //
  // This can be useful if more flexibility is needed than the provided
  // convenient methods can provide.
  // TODO(b/69412655): vectorize this.
  bool Recv(uint32_t seq, NetlinkParserInterface* parser) override;

 private:
  // Reset the size of recvbuf_ to size. If size is 0, recvbuf_ will be nullptr.
  void ResetRecvBuf(size_t size);

  // Opens a netlink socket if not already opened.
  bool OpenSocket();

  // Closes the opened netlink socket. Noop if no netlink socket is opened.
  void CloseSocket();

  KernelInterface* kernel_;
  int socket_fd_ = -1;
  std::unique_ptr<char[]> recvbuf_ = nullptr;
  size_t recvbuf_length_ = 0;
  uint32_t seq_;  // next msg sequence number
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_NETLINK_H_
