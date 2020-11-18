// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/rtnetlink_message.h"

#include <net/if_arp.h>

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace {

using ::testing::StrEq;

TEST(RtnetlinkMessageTest, LinkMessageCanBeCreatedForGetOperation) {
  uint16_t flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
  uint32_t seq = 42;
  uint32_t pid = 7;
  auto message = LinkMessage::New(RtnetlinkMessage::Operation::GET, flags, seq,
                                  pid, nullptr);

  // No rtattr appended.
  EXPECT_EQ(1, message.IoVecSize());

  // nlmsghdr is built properly.
  auto iov = message.BuildIoVec();
  EXPECT_EQ(NLMSG_SPACE(sizeof(struct rtgenmsg)), iov[0].iov_len);
  auto* netlink_message = reinterpret_cast<struct nlmsghdr*>(iov[0].iov_base);
  EXPECT_EQ(NLMSG_LENGTH(sizeof(struct rtgenmsg)), netlink_message->nlmsg_len);
  EXPECT_EQ(RTM_GETLINK, netlink_message->nlmsg_type);
  EXPECT_EQ(flags, netlink_message->nlmsg_flags);
  EXPECT_EQ(seq, netlink_message->nlmsg_seq);
  EXPECT_EQ(pid, netlink_message->nlmsg_pid);

  // We actually included rtgenmsg instead of the passed in ifinfomsg since this
  // is a GET operation.
  EXPECT_EQ(NLMSG_LENGTH(sizeof(struct rtgenmsg)), netlink_message->nlmsg_len);
}

TEST(RtnetlinkMessageTest, LinkMessageCanBeCreatedForNewOperation) {
  struct ifinfomsg interface_info_header = {AF_INET, /* pad */ 0, ARPHRD_TUNNEL,
                                            3,       0,           0xffffffff};
  uint16_t flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
  uint32_t seq = 42;
  uint32_t pid = 7;
  auto message = LinkMessage::New(RtnetlinkMessage::Operation::NEW, flags, seq,
                                  pid, &interface_info_header);

  std::string device_name = "device0";
  message.AppendAttribute(IFLA_IFNAME, device_name.c_str(), device_name.size());

  // One rtattr appended.
  EXPECT_EQ(2, message.IoVecSize());

  // nlmsghdr is built properly.
  auto iov = message.BuildIoVec();
  EXPECT_EQ(NLMSG_ALIGN(NLMSG_LENGTH(sizeof(struct ifinfomsg))),
            iov[0].iov_len);
  auto* netlink_message = reinterpret_cast<struct nlmsghdr*>(iov[0].iov_base);
  EXPECT_EQ(NLMSG_ALIGN(NLMSG_LENGTH(sizeof(struct ifinfomsg))) +
                RTA_LENGTH(device_name.size()),
            netlink_message->nlmsg_len);
  EXPECT_EQ(RTM_NEWLINK, netlink_message->nlmsg_type);
  EXPECT_EQ(flags, netlink_message->nlmsg_flags);
  EXPECT_EQ(seq, netlink_message->nlmsg_seq);
  EXPECT_EQ(pid, netlink_message->nlmsg_pid);

  // ifinfomsg is included properly.
  auto* parsed_header =
      reinterpret_cast<struct ifinfomsg*>(NLMSG_DATA(netlink_message));
  EXPECT_EQ(interface_info_header.ifi_family, parsed_header->ifi_family);
  EXPECT_EQ(interface_info_header.ifi_type, parsed_header->ifi_type);
  EXPECT_EQ(interface_info_header.ifi_index, parsed_header->ifi_index);
  EXPECT_EQ(interface_info_header.ifi_flags, parsed_header->ifi_flags);
  EXPECT_EQ(interface_info_header.ifi_change, parsed_header->ifi_change);

  // rtattr is handled properly.
  EXPECT_EQ(RTA_SPACE(device_name.size()), iov[1].iov_len);
  auto* rta = reinterpret_cast<struct rtattr*>(iov[1].iov_base);
  EXPECT_EQ(IFLA_IFNAME, rta->rta_type);
  EXPECT_EQ(RTA_LENGTH(device_name.size()), rta->rta_len);
  EXPECT_THAT(device_name,
              StrEq(std::string(reinterpret_cast<char*>(RTA_DATA(rta)),
                                RTA_PAYLOAD(rta))));
}

TEST(RtnetlinkMessageTest, AddressMessageCanBeCreatedForGetOperation) {
  uint16_t flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
  uint32_t seq = 42;
  uint32_t pid = 7;
  auto message = AddressMessage::New(RtnetlinkMessage::Operation::GET, flags,
                                     seq, pid, nullptr);

  // No rtattr appended.
  EXPECT_EQ(1, message.IoVecSize());

  // nlmsghdr is built properly.
  auto iov = message.BuildIoVec();
  EXPECT_EQ(NLMSG_SPACE(sizeof(struct rtgenmsg)), iov[0].iov_len);
  auto* netlink_message = reinterpret_cast<struct nlmsghdr*>(iov[0].iov_base);
  EXPECT_EQ(NLMSG_LENGTH(sizeof(struct rtgenmsg)), netlink_message->nlmsg_len);
  EXPECT_EQ(RTM_GETADDR, netlink_message->nlmsg_type);
  EXPECT_EQ(flags, netlink_message->nlmsg_flags);
  EXPECT_EQ(seq, netlink_message->nlmsg_seq);
  EXPECT_EQ(pid, netlink_message->nlmsg_pid);

  // We actually included rtgenmsg instead of the passed in ifinfomsg since this
  // is a GET operation.
  EXPECT_EQ(NLMSG_LENGTH(sizeof(struct rtgenmsg)), netlink_message->nlmsg_len);
}

TEST(RtnetlinkMessageTest, AddressMessageCanBeCreatedForNewOperation) {
  struct ifaddrmsg interface_address_header = {AF_INET,
                                               /* prefixlen */ 24,
                                               /* flags */ 0,
                                               /* scope */ RT_SCOPE_LINK,
                                               /* index */ 4};
  uint16_t flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
  uint32_t seq = 42;
  uint32_t pid = 7;
  auto message = AddressMessage::New(RtnetlinkMessage::Operation::NEW, flags,
                                     seq, pid, &interface_address_header);

  QuicIpAddress ip;
  CHECK(ip.FromString("10.0.100.3"));
  message.AppendAttribute(IFA_ADDRESS, ip.ToPackedString().c_str(),
                          ip.ToPackedString().size());

  // One rtattr is appended.
  EXPECT_EQ(2, message.IoVecSize());

  // nlmsghdr is built properly.
  auto iov = message.BuildIoVec();
  EXPECT_EQ(NLMSG_ALIGN(NLMSG_LENGTH(sizeof(struct ifaddrmsg))),
            iov[0].iov_len);
  auto* netlink_message = reinterpret_cast<struct nlmsghdr*>(iov[0].iov_base);
  EXPECT_EQ(NLMSG_ALIGN(NLMSG_LENGTH(sizeof(struct ifaddrmsg))) +
                RTA_LENGTH(ip.ToPackedString().size()),
            netlink_message->nlmsg_len);
  EXPECT_EQ(RTM_NEWADDR, netlink_message->nlmsg_type);
  EXPECT_EQ(flags, netlink_message->nlmsg_flags);
  EXPECT_EQ(seq, netlink_message->nlmsg_seq);
  EXPECT_EQ(pid, netlink_message->nlmsg_pid);

  // ifaddrmsg is included properly.
  auto* parsed_header =
      reinterpret_cast<struct ifaddrmsg*>(NLMSG_DATA(netlink_message));
  EXPECT_EQ(interface_address_header.ifa_family, parsed_header->ifa_family);
  EXPECT_EQ(interface_address_header.ifa_prefixlen,
            parsed_header->ifa_prefixlen);
  EXPECT_EQ(interface_address_header.ifa_flags, parsed_header->ifa_flags);
  EXPECT_EQ(interface_address_header.ifa_scope, parsed_header->ifa_scope);
  EXPECT_EQ(interface_address_header.ifa_index, parsed_header->ifa_index);

  // rtattr is handled properly.
  EXPECT_EQ(RTA_SPACE(ip.ToPackedString().size()), iov[1].iov_len);
  auto* rta = reinterpret_cast<struct rtattr*>(iov[1].iov_base);
  EXPECT_EQ(IFA_ADDRESS, rta->rta_type);
  EXPECT_EQ(RTA_LENGTH(ip.ToPackedString().size()), rta->rta_len);
  EXPECT_THAT(ip.ToPackedString(),
              StrEq(std::string(reinterpret_cast<char*>(RTA_DATA(rta)),
                                RTA_PAYLOAD(rta))));
}

TEST(RtnetlinkMessageTest, RouteMessageCanBeCreatedFromNewOperation) {
  struct rtmsg route_message_header = {AF_INET6,
                                       /* rtm_dst_len */ 48,
                                       /* rtm_src_len */ 0,
                                       /* rtm_tos */ 0,
                                       /* rtm_table */ RT_TABLE_MAIN,
                                       /* rtm_protocol */ RTPROT_STATIC,
                                       /* rtm_scope */ RT_SCOPE_LINK,
                                       /* rtm_type */ RTN_LOCAL,
                                       /* rtm_flags */ 0};
  uint16_t flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
  uint32_t seq = 42;
  uint32_t pid = 7;
  auto message = RouteMessage::New(RtnetlinkMessage::Operation::NEW, flags, seq,
                                   pid, &route_message_header);

  QuicIpAddress preferred_source;
  CHECK(preferred_source.FromString("ff80::1"));
  message.AppendAttribute(RTA_PREFSRC,
                          preferred_source.ToPackedString().c_str(),
                          preferred_source.ToPackedString().size());

  // One rtattr is appended.
  EXPECT_EQ(2, message.IoVecSize());

  // nlmsghdr is built properly
  auto iov = message.BuildIoVec();
  EXPECT_EQ(NLMSG_ALIGN(NLMSG_LENGTH(sizeof(struct rtmsg))), iov[0].iov_len);
  auto* netlink_message = reinterpret_cast<struct nlmsghdr*>(iov[0].iov_base);
  EXPECT_EQ(NLMSG_ALIGN(NLMSG_LENGTH(sizeof(struct rtmsg))) +
                RTA_LENGTH(preferred_source.ToPackedString().size()),
            netlink_message->nlmsg_len);
  EXPECT_EQ(RTM_NEWROUTE, netlink_message->nlmsg_type);
  EXPECT_EQ(flags, netlink_message->nlmsg_flags);
  EXPECT_EQ(seq, netlink_message->nlmsg_seq);
  EXPECT_EQ(pid, netlink_message->nlmsg_pid);

  // rtmsg is included properly.
  auto* parsed_header =
      reinterpret_cast<struct rtmsg*>(NLMSG_DATA(netlink_message));
  EXPECT_EQ(route_message_header.rtm_family, parsed_header->rtm_family);
  EXPECT_EQ(route_message_header.rtm_dst_len, parsed_header->rtm_dst_len);
  EXPECT_EQ(route_message_header.rtm_src_len, parsed_header->rtm_src_len);
  EXPECT_EQ(route_message_header.rtm_tos, parsed_header->rtm_tos);
  EXPECT_EQ(route_message_header.rtm_table, parsed_header->rtm_table);
  EXPECT_EQ(route_message_header.rtm_protocol, parsed_header->rtm_protocol);
  EXPECT_EQ(route_message_header.rtm_scope, parsed_header->rtm_scope);
  EXPECT_EQ(route_message_header.rtm_type, parsed_header->rtm_type);
  EXPECT_EQ(route_message_header.rtm_flags, parsed_header->rtm_flags);

  // rtattr is handled properly.
  EXPECT_EQ(RTA_SPACE(preferred_source.ToPackedString().size()),
            iov[1].iov_len);
  auto* rta = reinterpret_cast<struct rtattr*>(iov[1].iov_base);
  EXPECT_EQ(RTA_PREFSRC, rta->rta_type);
  EXPECT_EQ(RTA_LENGTH(preferred_source.ToPackedString().size()), rta->rta_len);
  EXPECT_THAT(preferred_source.ToPackedString(),
              StrEq(std::string(reinterpret_cast<char*>(RTA_DATA(rta)),
                                RTA_PAYLOAD(rta))));
}

}  // namespace
}  // namespace quic
