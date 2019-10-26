// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/netlink.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/platform/mock_kernel.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"

namespace quic {
namespace {

using ::testing::_;
using ::testing::Contains;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::Unused;

const int kSocketFd = 101;

class NetlinkTest : public QuicTest {
 protected:
  NetlinkTest() {
    ON_CALL(mock_kernel_, socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE))
        .WillByDefault(Invoke([this](Unused, Unused, Unused) {
          EXPECT_CALL(mock_kernel_, close(kSocketFd)).WillOnce(Return(0));
          return kSocketFd;
        }));
  }

  void ExpectNetlinkPacket(
      uint16_t type,
      uint16_t flags,
      const std::function<ssize_t(void* buf, size_t len, int seq)>&
          recv_callback,
      const std::function<void(const void* buf, size_t len)>& send_callback =
          nullptr) {
    static int seq = -1;
    InSequence s;

    EXPECT_CALL(mock_kernel_, sendmsg(kSocketFd, _, _))
        .WillOnce(Invoke([this, type, flags, send_callback](
                             Unused, const struct msghdr* msg, int) {
          EXPECT_EQ(sizeof(struct sockaddr_nl), msg->msg_namelen);
          auto* nl_addr =
              reinterpret_cast<const struct sockaddr_nl*>(msg->msg_name);
          EXPECT_EQ(AF_NETLINK, nl_addr->nl_family);
          EXPECT_EQ(0, nl_addr->nl_pid);
          EXPECT_EQ(0, nl_addr->nl_groups);

          EXPECT_GE(msg->msg_iovlen, 1);
          EXPECT_GE(msg->msg_iov[0].iov_len, sizeof(struct nlmsghdr));

          string buf;
          for (int i = 0; i < msg->msg_iovlen; i++) {
            buf.append(string(reinterpret_cast<char*>(msg->msg_iov[i].iov_base),
                              msg->msg_iov[i].iov_len));
          }

          auto* netlink_message =
              reinterpret_cast<const struct nlmsghdr*>(buf.c_str());
          EXPECT_EQ(type, netlink_message->nlmsg_type);
          EXPECT_EQ(flags, netlink_message->nlmsg_flags);
          EXPECT_GE(buf.size(), netlink_message->nlmsg_len);

          if (send_callback != nullptr) {
            send_callback(buf.c_str(), buf.size());
          }

          CHECK_EQ(seq, -1);
          seq = netlink_message->nlmsg_seq;
          return buf.size();
        }));

    EXPECT_CALL(mock_kernel_,
                recvfrom(kSocketFd, _, 0, MSG_PEEK | MSG_TRUNC, _, _))
        .WillOnce(Invoke([this, recv_callback](Unused, Unused, Unused, Unused,
                                               struct sockaddr* src_addr,
                                               socklen_t* addrlen) {
          auto* nl_addr = reinterpret_cast<struct sockaddr_nl*>(src_addr);
          nl_addr->nl_family = AF_NETLINK;
          nl_addr->nl_pid = 0;     // from kernel
          nl_addr->nl_groups = 0;  // no multicast

          int ret = recv_callback(reply_packet_, sizeof(reply_packet_), seq);
          CHECK_LE(ret, sizeof(reply_packet_));
          return ret;
        }));

    EXPECT_CALL(mock_kernel_, recvfrom(kSocketFd, _, _, _, _, _))
        .WillOnce(Invoke([recv_callback](Unused, void* buf, size_t len, Unused,
                                         struct sockaddr* src_addr,
                                         socklen_t* addrlen) {
          auto* nl_addr = reinterpret_cast<struct sockaddr_nl*>(src_addr);
          nl_addr->nl_family = AF_NETLINK;
          nl_addr->nl_pid = 0;     // from kernel
          nl_addr->nl_groups = 0;  // no multicast

          int ret = recv_callback(buf, len, seq);
          EXPECT_GE(len, ret);
          seq = -1;
          return ret;
        }));
  }

  char reply_packet_[4096];
  MockKernel mock_kernel_;
};

void AddRTA(struct nlmsghdr* netlink_message,
            uint16_t type,
            const void* data,
            size_t len) {
  auto* next_header_ptr = reinterpret_cast<char*>(netlink_message) +
                          NLMSG_ALIGN(netlink_message->nlmsg_len);

  auto* rta = reinterpret_cast<struct rtattr*>(next_header_ptr);
  rta->rta_type = type;
  rta->rta_len = RTA_LENGTH(len);
  memcpy(RTA_DATA(rta), data, len);

  netlink_message->nlmsg_len =
      NLMSG_ALIGN(netlink_message->nlmsg_len) + RTA_LENGTH(len);
}

void CreateIfinfomsg(struct nlmsghdr* netlink_message,
                     const string& interface_name,
                     uint16_t type,
                     int index,
                     unsigned int flags,
                     unsigned int change,
                     uint8_t address[],
                     int address_len,
                     uint8_t broadcast[],
                     int broadcast_len) {
  auto* interface_info =
      reinterpret_cast<struct ifinfomsg*>(NLMSG_DATA(netlink_message));
  interface_info->ifi_family = AF_UNSPEC;
  interface_info->ifi_type = type;
  interface_info->ifi_index = index;
  interface_info->ifi_flags = flags;
  interface_info->ifi_change = change;
  netlink_message->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));

  // Add address
  AddRTA(netlink_message, IFLA_ADDRESS, address, address_len);

  // Add broadcast address
  AddRTA(netlink_message, IFLA_BROADCAST, broadcast, broadcast_len);

  // Add name
  AddRTA(netlink_message, IFLA_IFNAME, interface_name.c_str(),
         interface_name.size());
}

struct nlmsghdr* CreateNetlinkMessage(void* buf,  // NOLINT
                                      struct nlmsghdr* previous_netlink_message,
                                      uint16_t type,
                                      int seq) {
  auto* next_header_ptr = reinterpret_cast<char*>(buf);
  if (previous_netlink_message != nullptr) {
    next_header_ptr = reinterpret_cast<char*>(previous_netlink_message) +
                      NLMSG_ALIGN(previous_netlink_message->nlmsg_len);
  }
  auto* netlink_message = reinterpret_cast<nlmsghdr*>(next_header_ptr);
  netlink_message->nlmsg_len = NLMSG_LENGTH(0);
  netlink_message->nlmsg_type = type;
  netlink_message->nlmsg_flags = NLM_F_MULTI;
  netlink_message->nlmsg_pid = 0;  // from the kernel
  netlink_message->nlmsg_seq = seq;

  return netlink_message;
}

void CreateIfaddrmsg(struct nlmsghdr* nlm,
                     int interface_index,
                     unsigned char prefixlen,
                     unsigned char flags,
                     unsigned char scope,
                     QuicIpAddress ip) {
  CHECK(ip.IsInitialized());
  unsigned char family;
  switch (ip.address_family()) {
    case IpAddressFamily::IP_V4:
      family = AF_INET;
      break;
    case IpAddressFamily::IP_V6:
      family = AF_INET6;
      break;
    default:
      QUIC_BUG << absl::StrCat("unexpected address family: ",
                               ip.address_family());
      family = AF_UNSPEC;
  }
  auto* msg = reinterpret_cast<struct ifaddrmsg*>(NLMSG_DATA(nlm));
  msg->ifa_family = family;
  msg->ifa_prefixlen = prefixlen;
  msg->ifa_flags = flags;
  msg->ifa_scope = scope;
  msg->ifa_index = interface_index;
  nlm->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));

  // Add local address
  AddRTA(nlm, IFA_LOCAL, ip.ToPackedString().c_str(),
         ip.ToPackedString().size());
}

void CreateRtmsg(struct nlmsghdr* nlm,
                 unsigned char family,
                 unsigned char destination_length,
                 unsigned char source_length,
                 unsigned char tos,
                 unsigned char table,
                 unsigned char protocol,
                 unsigned char scope,
                 unsigned char type,
                 unsigned int flags,
                 QuicIpAddress destination,
                 int interface_index) {
  auto* msg = reinterpret_cast<struct rtmsg*>(NLMSG_DATA(nlm));
  msg->rtm_family = family;
  msg->rtm_dst_len = destination_length;
  msg->rtm_src_len = source_length;
  msg->rtm_tos = tos;
  msg->rtm_table = table;
  msg->rtm_protocol = protocol;
  msg->rtm_scope = scope;
  msg->rtm_type = type;
  msg->rtm_flags = flags;
  nlm->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));

  // Add destination
  AddRTA(nlm, RTA_DST, destination.ToPackedString().c_str(),
         destination.ToPackedString().size());

  // Add egress interface
  AddRTA(nlm, RTA_OIF, &interface_index, sizeof(interface_index));
}

TEST_F(NetlinkTest, GetLinkInfoWorks) {
  auto netlink = QuicMakeUnique<Netlink>(&mock_kernel_);

  uint8_t hwaddr[] = {'a', 'b', 'c', 'd', 'e', 'f'};
  uint8_t bcaddr[] = {'c', 'b', 'a', 'f', 'e', 'd'};

  ExpectNetlinkPacket(
      RTM_GETLINK, NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST,
      [this, &hwaddr, &bcaddr](void* buf, size_t len, int seq) {
        int ret = 0;

        struct nlmsghdr* netlink_message =
            CreateNetlinkMessage(buf, nullptr, RTM_NEWLINK, seq);
        CreateIfinfomsg(netlink_message, "tun0", /* type = */ 1,
                        /* index = */ 7,
                        /* flags = */ 0,
                        /* change = */ 0xFFFFFFFF, hwaddr, 6, bcaddr, 6);
        ret += NLMSG_ALIGN(netlink_message->nlmsg_len);

        netlink_message =
            CreateNetlinkMessage(buf, netlink_message, NLMSG_DONE, seq);
        ret += NLMSG_ALIGN(netlink_message->nlmsg_len);

        return ret;
      });

  Netlink::LinkInfo link_info;
  EXPECT_TRUE(netlink->GetLinkInfo("tun0", &link_info));

  EXPECT_EQ(7, link_info.index);
  EXPECT_EQ(1, link_info.type);

  for (int i = 0; i < link_info.hardware_address_length; ++i) {
    EXPECT_EQ(hwaddr[i], link_info.hardware_address[i]);
  }
  for (int i = 0; i < link_info.broadcast_address_length; ++i) {
    EXPECT_EQ(bcaddr[i], link_info.broadcast_address[i]);
  }
}

TEST_F(NetlinkTest, GetAddressesWorks) {
  auto netlink = QuicMakeUnique<Netlink>(&mock_kernel_);

  QuicUnorderedSet<std::string> addresses = {QuicIpAddress::Any4().ToString(),
                                             QuicIpAddress::Any6().ToString()};

  ExpectNetlinkPacket(
      RTM_GETADDR, NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST,
      [this, &addresses](void* buf, size_t len, int seq) {
        int ret = 0;

        struct nlmsghdr* nlm = nullptr;

        for (const auto& address : addresses) {
          QuicIpAddress ip;
          ip.FromString(address);
          nlm = CreateNetlinkMessage(buf, nlm, RTM_NEWADDR, seq);
          CreateIfaddrmsg(nlm, /* interface_index = */ 7, /* prefixlen = */ 24,
                          /* flags = */ 0, /* scope = */ RT_SCOPE_UNIVERSE, ip);

          ret += NLMSG_ALIGN(nlm->nlmsg_len);
        }

        // Create IPs with unwanted flags.
        {
          QuicIpAddress ip;
          ip.FromString("10.0.0.1");
          nlm = CreateNetlinkMessage(buf, nlm, RTM_NEWADDR, seq);
          CreateIfaddrmsg(nlm, /* interface_index = */ 7, /* prefixlen = */ 16,
                          /* flags = */ IFA_F_OPTIMISTIC, /* scope = */
                          RT_SCOPE_UNIVERSE, ip);

          ret += NLMSG_ALIGN(nlm->nlmsg_len);

          ip.FromString("10.0.0.2");
          nlm = CreateNetlinkMessage(buf, nlm, RTM_NEWADDR, seq);
          CreateIfaddrmsg(nlm, /* interface_index = */ 7, /* prefixlen = */ 16,
                          /* flags = */ IFA_F_TENTATIVE, /* scope = */
                          RT_SCOPE_UNIVERSE, ip);

          ret += NLMSG_ALIGN(nlm->nlmsg_len);
        }

        nlm = CreateNetlinkMessage(buf, nlm, NLMSG_DONE, seq);
        ret += NLMSG_ALIGN(nlm->nlmsg_len);

        return ret;
      });

  std::vector<Netlink::AddressInfo> reported_addresses;
  int num_ipv6_nodad_dadfailed_addresses = 0;
  EXPECT_TRUE(netlink->GetAddresses(7, IFA_F_TENTATIVE | IFA_F_OPTIMISTIC,
                                    &reported_addresses,
                                    &num_ipv6_nodad_dadfailed_addresses));

  for (const auto& reported_address : reported_addresses) {
    EXPECT_TRUE(reported_address.local_address.IsInitialized());
    EXPECT_FALSE(reported_address.interface_address.IsInitialized());
    EXPECT_THAT(addresses, Contains(reported_address.local_address.ToString()));
    addresses.erase(reported_address.local_address.ToString());

    EXPECT_EQ(24, reported_address.prefix_length);
  }

  EXPECT_TRUE(addresses.empty());
}

TEST_F(NetlinkTest, ChangeLocalAddressAdd) {
  auto netlink = QuicMakeUnique<Netlink>(&mock_kernel_);

  QuicIpAddress ip = QuicIpAddress::Any6();
  ExpectNetlinkPacket(
      RTM_NEWADDR, NLM_F_ACK | NLM_F_REQUEST,
      [](void* buf, size_t len, int seq) {
        struct nlmsghdr* netlink_message =
            CreateNetlinkMessage(buf, nullptr, NLMSG_ERROR, seq);
        auto* err =
            reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(netlink_message));
        // Ack the request
        err->error = 0;
        netlink_message->nlmsg_len = NLMSG_LENGTH(sizeof(struct nlmsgerr));
        return netlink_message->nlmsg_len;
      },
      [ip](const void* buf, size_t len) {
        auto* netlink_message = reinterpret_cast<const struct nlmsghdr*>(buf);
        auto* ifa = reinterpret_cast<const struct ifaddrmsg*>(
            NLMSG_DATA(netlink_message));
        EXPECT_EQ(19, ifa->ifa_prefixlen);
        EXPECT_EQ(RT_SCOPE_UNIVERSE, ifa->ifa_scope);
        EXPECT_EQ(IFA_F_PERMANENT, ifa->ifa_flags);
        EXPECT_EQ(7, ifa->ifa_index);
        EXPECT_EQ(AF_INET6, ifa->ifa_family);

        const struct rtattr* rta;
        int payload_length = IFA_PAYLOAD(netlink_message);
        int num_rta = 0;
        for (rta = IFA_RTA(ifa); RTA_OK(rta, payload_length);
             rta = RTA_NEXT(rta, payload_length)) {
          switch (rta->rta_type) {
            case IFA_LOCAL: {
              EXPECT_EQ(ip.ToPackedString().size(), RTA_PAYLOAD(rta));
              const auto* raw_address =
                  reinterpret_cast<const char*>(RTA_DATA(rta));
              ASSERT_EQ(sizeof(in6_addr), RTA_PAYLOAD(rta));
              QuicIpAddress address;
              address.FromPackedString(raw_address, RTA_PAYLOAD(rta));
              EXPECT_EQ(ip, address);
              break;
            }
            case IFA_CACHEINFO: {
              EXPECT_EQ(sizeof(struct ifa_cacheinfo), RTA_PAYLOAD(rta));
              const auto* cache_info =
                  reinterpret_cast<const struct ifa_cacheinfo*>(RTA_DATA(rta));
              EXPECT_EQ(8, cache_info->ifa_prefered);  // common_typos_disable
              EXPECT_EQ(6, cache_info->ifa_valid);
              EXPECT_EQ(4, cache_info->cstamp);
              EXPECT_EQ(2, cache_info->tstamp);
              break;
            }
            default:
              EXPECT_TRUE(false) << "Seeing rtattr that should not exist";
          }
          ++num_rta;
        }
        EXPECT_EQ(2, num_rta);
      });

  struct {
    struct rtattr rta;
    struct ifa_cacheinfo cache_info;
  } additional_rta;

  additional_rta.rta.rta_type = IFA_CACHEINFO;
  additional_rta.rta.rta_len = RTA_LENGTH(sizeof(struct ifa_cacheinfo));
  additional_rta.cache_info.ifa_prefered = 8;
  additional_rta.cache_info.ifa_valid = 6;
  additional_rta.cache_info.cstamp = 4;
  additional_rta.cache_info.tstamp = 2;

  EXPECT_TRUE(netlink->ChangeLocalAddress(7, Netlink::Verb::kAdd, ip, 19,
                                          IFA_F_PERMANENT, RT_SCOPE_UNIVERSE,
                                          {&additional_rta.rta}));
}

TEST_F(NetlinkTest, ChangeLocalAddressRemove) {
  auto netlink = QuicMakeUnique<Netlink>(&mock_kernel_);

  QuicIpAddress ip = QuicIpAddress::Any4();
  ExpectNetlinkPacket(
      RTM_DELADDR, NLM_F_ACK | NLM_F_REQUEST,
      [](void* buf, size_t len, int seq) {
        struct nlmsghdr* netlink_message =
            CreateNetlinkMessage(buf, nullptr, NLMSG_ERROR, seq);
        auto* err =
            reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(netlink_message));
        // Ack the request
        err->error = 0;
        netlink_message->nlmsg_len = NLMSG_LENGTH(sizeof(struct nlmsgerr));
        return netlink_message->nlmsg_len;
      },
      [ip](const void* buf, size_t len) {
        auto* netlink_message = reinterpret_cast<const struct nlmsghdr*>(buf);
        auto* ifa = reinterpret_cast<const struct ifaddrmsg*>(
            NLMSG_DATA(netlink_message));
        EXPECT_EQ(32, ifa->ifa_prefixlen);
        EXPECT_EQ(RT_SCOPE_UNIVERSE, ifa->ifa_scope);
        EXPECT_EQ(0, ifa->ifa_flags);
        EXPECT_EQ(7, ifa->ifa_index);
        EXPECT_EQ(AF_INET, ifa->ifa_family);

        const struct rtattr* rta;
        int payload_length = IFA_PAYLOAD(netlink_message);
        int num_rta = 0;
        for (rta = IFA_RTA(ifa); RTA_OK(rta, payload_length);
             rta = RTA_NEXT(rta, payload_length)) {
          switch (rta->rta_type) {
            case IFA_LOCAL: {
              const auto* raw_address =
                  reinterpret_cast<const char*>(RTA_DATA(rta));
              ASSERT_EQ(sizeof(in_addr), RTA_PAYLOAD(rta));
              QuicIpAddress address;
              address.FromPackedString(raw_address, RTA_PAYLOAD(rta));
              EXPECT_EQ(ip, address);
              break;
            }
            default:
              EXPECT_TRUE(false) << "Seeing rtattr that should not exist";
          }
          ++num_rta;
        }
        EXPECT_EQ(1, num_rta);
      });

  EXPECT_TRUE(netlink->ChangeLocalAddress(7, Netlink::Verb::kRemove, ip, 32, 0,
                                          RT_SCOPE_UNIVERSE, {}));
}

TEST_F(NetlinkTest, GetRouteInfoWorks) {
  auto netlink = QuicMakeUnique<Netlink>(&mock_kernel_);

  QuicIpAddress destination;
  ASSERT_TRUE(destination.FromString("f800::2"));
  ExpectNetlinkPacket(RTM_GETROUTE, NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST,
                      [destination](void* buf, size_t len, int seq) {
                        int ret = 0;
                        struct nlmsghdr* netlink_message = CreateNetlinkMessage(
                            buf, nullptr, RTM_NEWROUTE, seq);
                        CreateRtmsg(netlink_message, AF_INET6, 48, 0, 0,
                                    RT_TABLE_MAIN, RTPROT_STATIC, RT_SCOPE_LINK,
                                    RTN_UNICAST, 0, destination, 7);
                        ret += NLMSG_ALIGN(netlink_message->nlmsg_len);

                        netlink_message = CreateNetlinkMessage(
                            buf, netlink_message, NLMSG_DONE, seq);
                        ret += NLMSG_ALIGN(netlink_message->nlmsg_len);

                        QUIC_LOG(INFO) << "ret: " << ret;
                        return ret;
                      });

  std::vector<Netlink::RoutingRule> routing_rules;
  EXPECT_TRUE(netlink->GetRouteInfo(&routing_rules));

  ASSERT_EQ(1, routing_rules.size());
  EXPECT_EQ(RT_SCOPE_LINK, routing_rules[0].scope);
  EXPECT_EQ(IpRange(destination, 48).ToString(),
            routing_rules[0].destination_subnet.ToString());
  EXPECT_FALSE(routing_rules[0].preferred_source.IsInitialized());
  EXPECT_EQ(7, routing_rules[0].out_interface);
}

TEST_F(NetlinkTest, ChangeRouteAdd) {
  auto netlink = QuicMakeUnique<Netlink>(&mock_kernel_);

  QuicIpAddress preferred_ip;
  preferred_ip.FromString("ff80:dead:beef::1");
  IpRange subnet;
  subnet.FromString("ff80:dead:beef::/48");
  int egress_interface_index = 7;
  ExpectNetlinkPacket(
      RTM_NEWROUTE, NLM_F_ACK | NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL,
      [](void* buf, size_t len, int seq) {
        struct nlmsghdr* netlink_message =
            CreateNetlinkMessage(buf, nullptr, NLMSG_ERROR, seq);
        auto* err =
            reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(netlink_message));
        // Ack the request
        err->error = 0;
        netlink_message->nlmsg_len = NLMSG_LENGTH(sizeof(struct nlmsgerr));
        return netlink_message->nlmsg_len;
      },
      [preferred_ip, subnet, egress_interface_index](const void* buf,
                                                     size_t len) {
        auto* netlink_message = reinterpret_cast<const struct nlmsghdr*>(buf);
        auto* rtm =
            reinterpret_cast<const struct rtmsg*>(NLMSG_DATA(netlink_message));
        EXPECT_EQ(AF_INET6, rtm->rtm_family);
        EXPECT_EQ(48, rtm->rtm_dst_len);
        EXPECT_EQ(0, rtm->rtm_src_len);
        EXPECT_EQ(RT_TABLE_MAIN, rtm->rtm_table);
        EXPECT_EQ(RTPROT_STATIC, rtm->rtm_protocol);
        EXPECT_EQ(RT_SCOPE_LINK, rtm->rtm_scope);
        EXPECT_EQ(RTN_UNICAST, rtm->rtm_type);

        const struct rtattr* rta;
        int payload_length = RTM_PAYLOAD(netlink_message);
        int num_rta = 0;
        for (rta = RTM_RTA(rtm); RTA_OK(rta, payload_length);
             rta = RTA_NEXT(rta, payload_length)) {
          switch (rta->rta_type) {
            case RTA_PREFSRC: {
              const auto* raw_address =
                  reinterpret_cast<const char*>(RTA_DATA(rta));
              ASSERT_EQ(sizeof(struct in6_addr), RTA_PAYLOAD(rta));
              QuicIpAddress address;
              address.FromPackedString(raw_address, RTA_PAYLOAD(rta));
              EXPECT_EQ(preferred_ip, address);
              break;
            }
            case RTA_OIF: {
              ASSERT_EQ(sizeof(int), RTA_PAYLOAD(rta));
              const auto* interface_index =
                  reinterpret_cast<const int*>(RTA_DATA(rta));
              EXPECT_EQ(egress_interface_index, *interface_index);
              break;
            }
            case RTA_DST: {
              const auto* raw_address =
                  reinterpret_cast<const char*>(RTA_DATA(rta));
              ASSERT_EQ(sizeof(struct in6_addr), RTA_PAYLOAD(rta));
              QuicIpAddress address;
              address.FromPackedString(raw_address, RTA_PAYLOAD(rta));
              EXPECT_EQ(subnet.ToString(),
                        IpRange(address, rtm->rtm_dst_len).ToString());
              break;
            }
            case RTA_TABLE: {
              ASSERT_EQ(*reinterpret_cast<uint32_t*>(RTA_DATA(rta)),
                        QboneConstants::kQboneRouteTableId);
              break;
            }
            default:
              EXPECT_TRUE(false) << "Seeing rtattr that should not be sent";
          }
          ++num_rta;
        }
        EXPECT_EQ(4, num_rta);
      });
  EXPECT_TRUE(netlink->ChangeRoute(
      Netlink::Verb::kAdd, QboneConstants::kQboneRouteTableId, subnet,
      RT_SCOPE_LINK, preferred_ip, egress_interface_index));
}

TEST_F(NetlinkTest, ChangeRouteRemove) {
  auto netlink = QuicMakeUnique<Netlink>(&mock_kernel_);

  QuicIpAddress preferred_ip;
  preferred_ip.FromString("ff80:dead:beef::1");
  IpRange subnet;
  subnet.FromString("ff80:dead:beef::/48");
  int egress_interface_index = 7;
  ExpectNetlinkPacket(
      RTM_DELROUTE, NLM_F_ACK | NLM_F_REQUEST,
      [](void* buf, size_t len, int seq) {
        struct nlmsghdr* netlink_message =
            CreateNetlinkMessage(buf, nullptr, NLMSG_ERROR, seq);
        auto* err =
            reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(netlink_message));
        // Ack the request
        err->error = 0;
        netlink_message->nlmsg_len = NLMSG_LENGTH(sizeof(struct nlmsgerr));
        return netlink_message->nlmsg_len;
      },
      [preferred_ip, subnet, egress_interface_index](const void* buf,
                                                     size_t len) {
        auto* netlink_message = reinterpret_cast<const struct nlmsghdr*>(buf);
        auto* rtm =
            reinterpret_cast<const struct rtmsg*>(NLMSG_DATA(netlink_message));
        EXPECT_EQ(AF_INET6, rtm->rtm_family);
        EXPECT_EQ(48, rtm->rtm_dst_len);
        EXPECT_EQ(0, rtm->rtm_src_len);
        EXPECT_EQ(RT_TABLE_MAIN, rtm->rtm_table);
        EXPECT_EQ(RTPROT_UNSPEC, rtm->rtm_protocol);
        EXPECT_EQ(RT_SCOPE_LINK, rtm->rtm_scope);
        EXPECT_EQ(RTN_UNICAST, rtm->rtm_type);

        const struct rtattr* rta;
        int payload_length = RTM_PAYLOAD(netlink_message);
        int num_rta = 0;
        for (rta = RTM_RTA(rtm); RTA_OK(rta, payload_length);
             rta = RTA_NEXT(rta, payload_length)) {
          switch (rta->rta_type) {
            case RTA_PREFSRC: {
              const auto* raw_address =
                  reinterpret_cast<const char*>(RTA_DATA(rta));
              ASSERT_EQ(sizeof(struct in6_addr), RTA_PAYLOAD(rta));
              QuicIpAddress address;
              address.FromPackedString(raw_address, RTA_PAYLOAD(rta));
              EXPECT_EQ(preferred_ip, address);
              break;
            }
            case RTA_OIF: {
              ASSERT_EQ(sizeof(int), RTA_PAYLOAD(rta));
              const auto* interface_index =
                  reinterpret_cast<const int*>(RTA_DATA(rta));
              EXPECT_EQ(egress_interface_index, *interface_index);
              break;
            }
            case RTA_DST: {
              const auto* raw_address =
                  reinterpret_cast<const char*>(RTA_DATA(rta));
              ASSERT_EQ(sizeof(struct in6_addr), RTA_PAYLOAD(rta));
              QuicIpAddress address;
              address.FromPackedString(raw_address, RTA_PAYLOAD(rta));
              EXPECT_EQ(subnet.ToString(),
                        IpRange(address, rtm->rtm_dst_len).ToString());
              break;
            }
            case RTA_TABLE: {
              ASSERT_EQ(*reinterpret_cast<uint32_t*>(RTA_DATA(rta)),
                        QboneConstants::kQboneRouteTableId);
              break;
            }
            default:
              EXPECT_TRUE(false) << "Seeing rtattr that should not be sent";
          }
          ++num_rta;
        }
        EXPECT_EQ(4, num_rta);
      });
  EXPECT_TRUE(netlink->ChangeRoute(
      Netlink::Verb::kRemove, QboneConstants::kQboneRouteTableId, subnet,
      RT_SCOPE_LINK, preferred_ip, egress_interface_index));
}

TEST_F(NetlinkTest, ChangeRouteReplace) {
  auto netlink = QuicMakeUnique<Netlink>(&mock_kernel_);

  QuicIpAddress preferred_ip;
  preferred_ip.FromString("ff80:dead:beef::1");
  IpRange subnet;
  subnet.FromString("ff80:dead:beef::/48");
  int egress_interface_index = 7;
  ExpectNetlinkPacket(
      RTM_NEWROUTE, NLM_F_ACK | NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE,
      [](void* buf, size_t len, int seq) {
        struct nlmsghdr* netlink_message =
            CreateNetlinkMessage(buf, nullptr, NLMSG_ERROR, seq);
        auto* err =
            reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(netlink_message));
        // Ack the request
        err->error = 0;
        netlink_message->nlmsg_len = NLMSG_LENGTH(sizeof(struct nlmsgerr));
        return netlink_message->nlmsg_len;
      },
      [preferred_ip, subnet, egress_interface_index](const void* buf,
                                                     size_t len) {
        auto* netlink_message = reinterpret_cast<const struct nlmsghdr*>(buf);
        auto* rtm =
            reinterpret_cast<const struct rtmsg*>(NLMSG_DATA(netlink_message));
        EXPECT_EQ(AF_INET6, rtm->rtm_family);
        EXPECT_EQ(48, rtm->rtm_dst_len);
        EXPECT_EQ(0, rtm->rtm_src_len);
        EXPECT_EQ(RT_TABLE_MAIN, rtm->rtm_table);
        EXPECT_EQ(RTPROT_STATIC, rtm->rtm_protocol);
        EXPECT_EQ(RT_SCOPE_LINK, rtm->rtm_scope);
        EXPECT_EQ(RTN_UNICAST, rtm->rtm_type);

        const struct rtattr* rta;
        int payload_length = RTM_PAYLOAD(netlink_message);
        int num_rta = 0;
        for (rta = RTM_RTA(rtm); RTA_OK(rta, payload_length);
             rta = RTA_NEXT(rta, payload_length)) {
          switch (rta->rta_type) {
            case RTA_PREFSRC: {
              const auto* raw_address =
                  reinterpret_cast<const char*>(RTA_DATA(rta));
              ASSERT_EQ(sizeof(struct in6_addr), RTA_PAYLOAD(rta));
              QuicIpAddress address;
              address.FromPackedString(raw_address, RTA_PAYLOAD(rta));
              EXPECT_EQ(preferred_ip, address);
              break;
            }
            case RTA_OIF: {
              ASSERT_EQ(sizeof(int), RTA_PAYLOAD(rta));
              const auto* interface_index =
                  reinterpret_cast<const int*>(RTA_DATA(rta));
              EXPECT_EQ(egress_interface_index, *interface_index);
              break;
            }
            case RTA_DST: {
              const auto* raw_address =
                  reinterpret_cast<const char*>(RTA_DATA(rta));
              ASSERT_EQ(sizeof(struct in6_addr), RTA_PAYLOAD(rta));
              QuicIpAddress address;
              address.FromPackedString(raw_address, RTA_PAYLOAD(rta));
              EXPECT_EQ(subnet.ToString(),
                        IpRange(address, rtm->rtm_dst_len).ToString());
              break;
            }
            case RTA_TABLE: {
              ASSERT_EQ(*reinterpret_cast<uint32_t*>(RTA_DATA(rta)),
                        QboneConstants::kQboneRouteTableId);
              break;
            }
            default:
              EXPECT_TRUE(false) << "Seeing rtattr that should not be sent";
          }
          ++num_rta;
        }
        EXPECT_EQ(4, num_rta);
      });
  EXPECT_TRUE(netlink->ChangeRoute(
      Netlink::Verb::kReplace, QboneConstants::kQboneRouteTableId, subnet,
      RT_SCOPE_LINK, preferred_ip, egress_interface_index));
}

}  // namespace
}  // namespace quic
