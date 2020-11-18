// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/netlink.h"

#include <linux/fib_rules.h>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fallthrough.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/quic/platform/impl/quic_ip_address_impl.h"
#include "net/third_party/quiche/src/quic/qbone/platform/rtnetlink_message.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

Netlink::Netlink(KernelInterface* kernel) : kernel_(kernel) {
  seq_ = QuicRandom::GetInstance()->RandUint64();
}

Netlink::~Netlink() {
  CloseSocket();
}

void Netlink::ResetRecvBuf(size_t size) {
  if (size != 0) {
    recvbuf_ = std::make_unique<char[]>(size);
  } else {
    recvbuf_ = nullptr;
  }
  recvbuf_length_ = size;
}

bool Netlink::OpenSocket() {
  if (socket_fd_ >= 0) {
    return true;
  }

  socket_fd_ = kernel_->socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

  if (socket_fd_ < 0) {
    QUIC_PLOG(ERROR) << "can't open netlink socket";
    return false;
  }

  QUIC_LOG(INFO) << "Opened a new netlink socket fd = " << socket_fd_;

  // bind a local address to the socket
  sockaddr_nl myaddr;
  memset(&myaddr, 0, sizeof(myaddr));
  myaddr.nl_family = AF_NETLINK;
  if (kernel_->bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&myaddr),
                    sizeof(myaddr)) < 0) {
    QUIC_LOG(INFO) << "can't bind address to socket";
    CloseSocket();
    return false;
  }

  return true;
}

void Netlink::CloseSocket() {
  if (socket_fd_ >= 0) {
    QUIC_LOG(INFO) << "Closing netlink socket fd = " << socket_fd_;
    kernel_->close(socket_fd_);
  }
  ResetRecvBuf(0);
  socket_fd_ = -1;
}

namespace {

class LinkInfoParser : public NetlinkParserInterface {
 public:
  LinkInfoParser(std::string interface_name, Netlink::LinkInfo* link_info)
      : interface_name_(std::move(interface_name)), link_info_(link_info) {}

  void Run(struct nlmsghdr* netlink_message) override {
    if (netlink_message->nlmsg_type != RTM_NEWLINK) {
      QUIC_LOG(INFO) << quiche::QuicheStrCat(
          "Unexpected nlmsg_type: ", netlink_message->nlmsg_type,
          " expected: ", RTM_NEWLINK);
      return;
    }

    struct ifinfomsg* interface_info =
        reinterpret_cast<struct ifinfomsg*>(NLMSG_DATA(netlink_message));

    // make sure interface_info is what we asked for.
    if (interface_info->ifi_family != AF_UNSPEC) {
      QUIC_LOG(INFO) << quiche::QuicheStrCat(
          "Unexpected ifi_family: ", interface_info->ifi_family,
          " expected: ", AF_UNSPEC);
      return;
    }

    char hardware_address[kHwAddrSize];
    size_t hardware_address_length = 0;
    char broadcast_address[kHwAddrSize];
    size_t broadcast_address_length = 0;
    std::string name;

    // loop through the attributes
    struct rtattr* rta;
    int payload_length = IFLA_PAYLOAD(netlink_message);
    for (rta = IFLA_RTA(interface_info); RTA_OK(rta, payload_length);
         rta = RTA_NEXT(rta, payload_length)) {
      int attribute_length;
      switch (rta->rta_type) {
        case IFLA_ADDRESS: {
          attribute_length = RTA_PAYLOAD(rta);
          if (attribute_length > kHwAddrSize) {
            QUIC_VLOG(2) << "IFLA_ADDRESS too long: " << attribute_length;
            break;
          }
          memmove(hardware_address, RTA_DATA(rta), attribute_length);
          hardware_address_length = attribute_length;
          break;
        }
        case IFLA_BROADCAST: {
          attribute_length = RTA_PAYLOAD(rta);
          if (attribute_length > kHwAddrSize) {
            QUIC_VLOG(2) << "IFLA_BROADCAST too long: " << attribute_length;
            break;
          }
          memmove(broadcast_address, RTA_DATA(rta), attribute_length);
          broadcast_address_length = attribute_length;
          break;
        }
        case IFLA_IFNAME: {
          name = std::string(reinterpret_cast<char*>(RTA_DATA(rta)),
                             RTA_PAYLOAD(rta));
          // The name maybe a 0 terminated c string.
          name = name.substr(0, name.find('\0'));
          break;
        }
      }
    }

    QUIC_VLOG(2) << "interface name: " << name
                 << ", index: " << interface_info->ifi_index;

    if (name == interface_name_) {
      link_info_->index = interface_info->ifi_index;
      link_info_->type = interface_info->ifi_type;
      link_info_->hardware_address_length = hardware_address_length;
      if (hardware_address_length > 0) {
        memmove(&link_info_->hardware_address, hardware_address,
                hardware_address_length);
      }
      link_info_->broadcast_address_length = broadcast_address_length;
      if (broadcast_address_length > 0) {
        memmove(&link_info_->broadcast_address, broadcast_address,
                broadcast_address_length);
      }
      found_link_ = true;
    }
  }

  bool found_link() { return found_link_; }

 private:
  const std::string interface_name_;
  Netlink::LinkInfo* const link_info_;
  bool found_link_ = false;
};

}  // namespace

bool Netlink::GetLinkInfo(const std::string& interface_name,
                          LinkInfo* link_info) {
  auto message = LinkMessage::New(RtnetlinkMessage::Operation::GET,
                                  NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST,
                                  seq_, getpid(), nullptr);

  if (!Send(message.BuildIoVec().get(), message.IoVecSize())) {
    QUIC_LOG(ERROR) << "send failed.";
    return false;
  }

  // Pass the parser to the receive routine. It may be called multiple times
  // since there may be multiple reply packets each with multiple reply
  // messages.
  LinkInfoParser parser(interface_name, link_info);
  if (!Recv(seq_++, &parser)) {
    QUIC_LOG(ERROR) << "recv failed.";
    return false;
  }

  return parser.found_link();
}

namespace {

class LocalAddressParser : public NetlinkParserInterface {
 public:
  LocalAddressParser(int interface_index,
                     uint8_t unwanted_flags,
                     std::vector<Netlink::AddressInfo>* local_addresses,
                     int* num_ipv6_nodad_dadfailed_addresses)
      : interface_index_(interface_index),
        unwanted_flags_(unwanted_flags),
        local_addresses_(local_addresses),
        num_ipv6_nodad_dadfailed_addresses_(
            num_ipv6_nodad_dadfailed_addresses) {}

  void Run(struct nlmsghdr* netlink_message) override {
    // each nlmsg contains a header and multiple address attributes.
    if (netlink_message->nlmsg_type != RTM_NEWADDR) {
      QUIC_LOG(INFO) << "Unexpected nlmsg_type: " << netlink_message->nlmsg_type
                     << " expected: " << RTM_NEWADDR;
      return;
    }

    struct ifaddrmsg* interface_address =
        reinterpret_cast<struct ifaddrmsg*>(NLMSG_DATA(netlink_message));

    // Make sure this is for an address family we're interested in.
    if (interface_address->ifa_family != AF_INET &&
        interface_address->ifa_family != AF_INET6) {
      QUIC_VLOG(2) << quiche::QuicheStrCat("uninteresting ifa family: ",
                                           interface_address->ifa_family);
      return;
    }

    // Keep track of addresses with both 'nodad' and 'dadfailed', this really
    // should't be possible and is likely a kernel bug.
    if (num_ipv6_nodad_dadfailed_addresses_ != nullptr &&
        (interface_address->ifa_flags & IFA_F_NODAD) &&
        (interface_address->ifa_flags & IFA_F_DADFAILED)) {
      ++(*num_ipv6_nodad_dadfailed_addresses_);
    }

    uint8_t unwanted_flags = interface_address->ifa_flags & unwanted_flags_;
    if (unwanted_flags != 0) {
      QUIC_VLOG(2) << quiche::QuicheStrCat("unwanted ifa flags: ",
                                           unwanted_flags);
      return;
    }

    // loop through the attributes
    struct rtattr* rta;
    int payload_length = IFA_PAYLOAD(netlink_message);
    Netlink::AddressInfo address_info;
    for (rta = IFA_RTA(interface_address); RTA_OK(rta, payload_length);
         rta = RTA_NEXT(rta, payload_length)) {
      // There's quite a lot of confusion in Linux over the use of IFA_LOCAL and
      // IFA_ADDRESS (source and destination address). For broadcast links, such
      // as Ethernet, they are identical (see <linux/if_addr.h>), but the kernel
      // sometimes uses only one or the other. We'll return both so that the
      // caller can decide which to use.
      if (rta->rta_type != IFA_LOCAL && rta->rta_type != IFA_ADDRESS) {
        QUIC_VLOG(2) << "Ignoring uninteresting rta_type: " << rta->rta_type;
        continue;
      }

      switch (interface_address->ifa_family) {
        case AF_INET:
          QUIC_FALLTHROUGH_INTENDED;
        case AF_INET6:
          // QuicIpAddress knows how to parse ip from raw bytes as long as they
          // are in network byte order.
          if (RTA_PAYLOAD(rta) == sizeof(struct in_addr) ||
              RTA_PAYLOAD(rta) == sizeof(struct in6_addr)) {
            auto* raw_ip = reinterpret_cast<char*>(RTA_DATA(rta));
            if (rta->rta_type == IFA_LOCAL) {
              address_info.local_address.FromPackedString(raw_ip,
                                                          RTA_PAYLOAD(rta));
            } else {
              address_info.interface_address.FromPackedString(raw_ip,
                                                              RTA_PAYLOAD(rta));
            }
          }
          break;
        default:
          QUIC_LOG(ERROR) << quiche::QuicheStrCat(
              "Unknown address family: ", interface_address->ifa_family);
      }
    }

    QUIC_VLOG(2) << "local_address: " << address_info.local_address.ToString()
                 << " interface_address: "
                 << address_info.interface_address.ToString()
                 << " index: " << interface_address->ifa_index;
    if (interface_address->ifa_index != interface_index_) {
      return;
    }

    address_info.prefix_length = interface_address->ifa_prefixlen;
    address_info.scope = interface_address->ifa_scope;
    if (address_info.local_address.IsInitialized() ||
        address_info.interface_address.IsInitialized()) {
      local_addresses_->push_back(address_info);
    }
  }

 private:
  const int interface_index_;
  const uint8_t unwanted_flags_;
  std::vector<Netlink::AddressInfo>* const local_addresses_;
  int* const num_ipv6_nodad_dadfailed_addresses_;
};

}  // namespace

bool Netlink::GetAddresses(int interface_index,
                           uint8_t unwanted_flags,
                           std::vector<AddressInfo>* addresses,
                           int* num_ipv6_nodad_dadfailed_addresses) {
  // the message doesn't contain the index, we'll have to do the filtering while
  // parsing the reply. This is because NLM_F_MATCH, which only returns entries
  // that matches the request criteria, is not yet implemented (see man 3
  // netlink).
  auto message = AddressMessage::New(RtnetlinkMessage::Operation::GET,
                                     NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST,
                                     seq_, getpid(), nullptr);

  // the send routine returns the socket to listen on.
  if (!Send(message.BuildIoVec().get(), message.IoVecSize())) {
    QUIC_LOG(ERROR) << "send failed.";
    return false;
  }

  addresses->clear();
  if (num_ipv6_nodad_dadfailed_addresses != nullptr) {
    *num_ipv6_nodad_dadfailed_addresses = 0;
  }

  LocalAddressParser parser(interface_index, unwanted_flags, addresses,
                            num_ipv6_nodad_dadfailed_addresses);
  // Pass the parser to the receive routine. It may be called multiple times
  // since there may be multiple reply packets each with multiple reply
  // messages.
  if (!Recv(seq_++, &parser)) {
    QUIC_LOG(ERROR) << "recv failed";
    return false;
  }
  return true;
}

namespace {

class UnknownParser : public NetlinkParserInterface {
 public:
  void Run(struct nlmsghdr* netlink_message) override {
    QUIC_LOG(INFO) << "nlmsg reply type: " << netlink_message->nlmsg_type;
  }
};

}  // namespace

bool Netlink::ChangeLocalAddress(
    uint32_t interface_index,
    Verb verb,
    const QuicIpAddress& address,
    uint8_t prefix_length,
    uint8_t ifa_flags,
    uint8_t ifa_scope,
    const std::vector<struct rtattr*>& additional_attributes) {
  if (verb == Verb::kReplace) {
    return false;
  }
  auto operation = verb == Verb::kAdd ? RtnetlinkMessage::Operation::NEW
                                      : RtnetlinkMessage::Operation::DEL;
  uint8_t address_family;
  if (address.address_family() == IpAddressFamily::IP_V4) {
    address_family = AF_INET;
  } else if (address.address_family() == IpAddressFamily::IP_V6) {
    address_family = AF_INET6;
  } else {
    return false;
  }

  struct ifaddrmsg address_header = {address_family, prefix_length, ifa_flags,
                                     ifa_scope, interface_index};

  auto message = AddressMessage::New(operation, NLM_F_REQUEST | NLM_F_ACK, seq_,
                                     getpid(), &address_header);

  for (const auto& attribute : additional_attributes) {
    if (attribute->rta_type == IFA_LOCAL) {
      continue;
    }
    message.AppendAttribute(attribute->rta_type, RTA_DATA(attribute),
                            RTA_PAYLOAD(attribute));
  }

  message.AppendAttribute(IFA_LOCAL, address.ToPackedString().c_str(),
                          address.ToPackedString().size());

  if (!Send(message.BuildIoVec().get(), message.IoVecSize())) {
    QUIC_LOG(ERROR) << "send failed";
    return false;
  }

  UnknownParser parser;
  if (!Recv(seq_++, &parser)) {
    QUIC_LOG(ERROR) << "receive failed.";
    return false;
  }
  return true;
}

namespace {

class RoutingRuleParser : public NetlinkParserInterface {
 public:
  explicit RoutingRuleParser(std::vector<Netlink::RoutingRule>* routing_rules)
      : routing_rules_(routing_rules) {}

  void Run(struct nlmsghdr* netlink_message) override {
    if (netlink_message->nlmsg_type != RTM_NEWROUTE) {
      QUIC_LOG(WARNING) << quiche::QuicheStrCat(
          "Unexpected nlmsg_type: ", netlink_message->nlmsg_type,
          " expected: ", RTM_NEWROUTE);
      return;
    }

    auto* route = reinterpret_cast<struct rtmsg*>(NLMSG_DATA(netlink_message));
    int payload_length = RTM_PAYLOAD(netlink_message);

    if (route->rtm_family != AF_INET && route->rtm_family != AF_INET6) {
      QUIC_VLOG(2) << quiche::QuicheStrCat("Uninteresting family: ",
                                           route->rtm_family);
      return;
    }

    Netlink::RoutingRule rule;
    rule.scope = route->rtm_scope;
    rule.table = route->rtm_table;

    struct rtattr* rta;
    for (rta = RTM_RTA(route); RTA_OK(rta, payload_length);
         rta = RTA_NEXT(rta, payload_length)) {
      switch (rta->rta_type) {
        case RTA_TABLE: {
          rule.table = *reinterpret_cast<uint32_t*>(RTA_DATA(rta));
          break;
        }
        case RTA_DST: {
          QuicIpAddress destination;
          destination.FromPackedString(reinterpret_cast<char*> RTA_DATA(rta),
                                       RTA_PAYLOAD(rta));
          rule.destination_subnet = IpRange(destination, route->rtm_dst_len);
          break;
        }
        case RTA_PREFSRC: {
          QuicIpAddress preferred_source;
          rule.preferred_source.FromPackedString(
              reinterpret_cast<char*> RTA_DATA(rta), RTA_PAYLOAD(rta));
          break;
        }
        case RTA_OIF: {
          rule.out_interface = *reinterpret_cast<int*>(RTA_DATA(rta));
          break;
        }
        default: {
          QUIC_VLOG(2) << quiche::QuicheStrCat("Uninteresting attribute: ",
                                               rta->rta_type);
        }
      }
    }
    routing_rules_->push_back(rule);
  }

 private:
  std::vector<Netlink::RoutingRule>* routing_rules_;
};

}  // namespace

bool Netlink::GetRouteInfo(std::vector<Netlink::RoutingRule>* routing_rules) {
  rtmsg route_message{};
  // Only manipulate main routing table.
  route_message.rtm_table = RT_TABLE_MAIN;

  auto message = RouteMessage::New(RtnetlinkMessage::Operation::GET,
                                   NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH,
                                   seq_, getpid(), &route_message);

  if (!Send(message.BuildIoVec().get(), message.IoVecSize())) {
    QUIC_LOG(ERROR) << "send failed";
    return false;
  }

  RoutingRuleParser parser(routing_rules);
  if (!Recv(seq_++, &parser)) {
    QUIC_LOG(ERROR) << "recv failed";
    return false;
  }

  return true;
}

bool Netlink::ChangeRoute(Netlink::Verb verb,
                          uint32_t table,
                          const IpRange& destination_subnet,
                          uint8_t scope,
                          QuicIpAddress preferred_source,
                          int32_t interface_index) {
  if (!destination_subnet.prefix().IsInitialized()) {
    return false;
  }
  if (destination_subnet.address_family() != IpAddressFamily::IP_V4 &&
      destination_subnet.address_family() != IpAddressFamily::IP_V6) {
    return false;
  }
  if (preferred_source.IsInitialized() &&
      preferred_source.address_family() !=
          destination_subnet.address_family()) {
    return false;
  }

  RtnetlinkMessage::Operation operation;
  uint16_t flags = NLM_F_REQUEST | NLM_F_ACK;
  switch (verb) {
    case Verb::kAdd:
      operation = RtnetlinkMessage::Operation::NEW;
      // Setting NLM_F_EXCL so that an existing entry for this subnet will fail
      // the request. NLM_F_CREATE is necessary to indicate this is trying to
      // create a new entry - simply having RTM_NEWROUTE is not enough even the
      // name suggests so.
      flags |= NLM_F_EXCL | NLM_F_CREATE;
      break;
    case Verb::kRemove:
      operation = RtnetlinkMessage::Operation::DEL;
      break;
    case Verb::kReplace:
      operation = RtnetlinkMessage::Operation::NEW;
      // Setting NLM_F_REPLACE to tell the kernel that existing entry for this
      // subnet should be replaced.
      flags |= NLM_F_REPLACE | NLM_F_CREATE;
      break;
  }

  struct rtmsg route_message;
  memset(&route_message, 0, sizeof(route_message));
  route_message.rtm_family =
      destination_subnet.address_family() == IpAddressFamily::IP_V4 ? AF_INET
                                                                    : AF_INET6;
  // rtm_dst_len and rtm_src_len are actually the subnet prefix lengths. Poor
  // naming.
  route_message.rtm_dst_len = destination_subnet.prefix_length();
  // 0 means no source subnet for this rule.
  route_message.rtm_src_len = 0;
  // Only program the main table. Other tables are intended for the kernel to
  // manage.
  route_message.rtm_table = RT_TABLE_MAIN;
  // Use RTPROT_UNSPEC to match all the different protocol. Rules added by
  // kernel have RTPROT_KERNEL. Rules added by the root user have RTPROT_STATIC
  // instead.
  route_message.rtm_protocol =
      verb == Verb::kRemove ? RTPROT_UNSPEC : RTPROT_STATIC;
  route_message.rtm_scope = scope;
  // Only add unicast routing rule.
  route_message.rtm_type = RTN_UNICAST;
  auto message =
      RouteMessage::New(operation, flags, seq_, getpid(), &route_message);

  message.AppendAttribute(RTA_TABLE, &table, sizeof(table));

  // RTA_OIF is the target interface for this rule.
  message.AppendAttribute(RTA_OIF, &interface_index, sizeof(interface_index));
  // The actual destination subnet must be truncated of all the tailing zeros.
  message.AppendAttribute(
      RTA_DST,
      reinterpret_cast<const void*>(
          destination_subnet.prefix().ToPackedString().c_str()),
      destination_subnet.prefix().ToPackedString().size());
  // This is the source address to use in the IP packet should this routing rule
  // is used.
  if (preferred_source.IsInitialized()) {
    message.AppendAttribute(RTA_PREFSRC,
                            reinterpret_cast<const void*>(
                                preferred_source.ToPackedString().c_str()),
                            preferred_source.ToPackedString().size());
  }

  if (!Send(message.BuildIoVec().get(), message.IoVecSize())) {
    QUIC_LOG(ERROR) << "send failed";
    return false;
  }

  UnknownParser parser;
  if (!Recv(seq_++, &parser)) {
    QUIC_LOG(ERROR) << "receive failed.";
    return false;
  }
  return true;
}

namespace {

class IpRuleParser : public NetlinkParserInterface {
 public:
  explicit IpRuleParser(std::vector<Netlink::IpRule>* ip_rules)
      : ip_rules_(ip_rules) {}

  void Run(struct nlmsghdr* netlink_message) override {
    if (netlink_message->nlmsg_type != RTM_NEWRULE) {
      QUIC_LOG(WARNING) << quiche::QuicheStrCat(
          "Unexpected nlmsg_type: ", netlink_message->nlmsg_type,
          " expected: ", RTM_NEWRULE);
      return;
    }

    auto* rule = reinterpret_cast<rtmsg*>(NLMSG_DATA(netlink_message));
    int payload_length = RTM_PAYLOAD(netlink_message);

    if (rule->rtm_family != AF_INET6) {
      QUIC_LOG(ERROR) << quiche::QuicheStrCat("Unexpected family: ",
                                              rule->rtm_family);
      return;
    }

    Netlink::IpRule ip_rule;
    ip_rule.table = rule->rtm_table;

    struct rtattr* rta;
    for (rta = RTM_RTA(rule); RTA_OK(rta, payload_length);
         rta = RTA_NEXT(rta, payload_length)) {
      switch (rta->rta_type) {
        case RTA_TABLE: {
          ip_rule.table = *reinterpret_cast<uint32_t*>(RTA_DATA(rta));
          break;
        }
        case RTA_SRC: {
          QuicIpAddress src_addr;
          src_addr.FromPackedString(reinterpret_cast<char*>(RTA_DATA(rta)),
                                    RTA_PAYLOAD(rta));
          IpRange src_range(src_addr, rule->rtm_src_len);
          ip_rule.source_range = src_range;
          break;
        }
        default: {
          QUIC_VLOG(2) << quiche::QuicheStrCat("Uninteresting attribute: ",
                                               rta->rta_type);
        }
      }
    }
    ip_rules_->emplace_back(ip_rule);
  }

 private:
  std::vector<Netlink::IpRule>* ip_rules_;
};

}  // namespace

bool Netlink::GetRuleInfo(std::vector<Netlink::IpRule>* ip_rules) {
  rtmsg rule_message{};
  rule_message.rtm_family = AF_INET6;

  auto message = RuleMessage::New(RtnetlinkMessage::Operation::GET,
                                  NLM_F_REQUEST | NLM_F_DUMP, seq_, getpid(),
                                  &rule_message);

  if (!Send(message.BuildIoVec().get(), message.IoVecSize())) {
    QUIC_LOG(ERROR) << "send failed";
    return false;
  }

  IpRuleParser parser(ip_rules);
  if (!Recv(seq_++, &parser)) {
    QUIC_LOG(ERROR) << "receive failed.";
    return false;
  }
  return true;
}

bool Netlink::ChangeRule(Verb verb, uint32_t table, IpRange source_range) {
  RtnetlinkMessage::Operation operation;
  uint16_t flags = NLM_F_REQUEST | NLM_F_ACK;

  rtmsg rule_message{};
  rule_message.rtm_family = AF_INET6;
  rule_message.rtm_protocol = RTPROT_STATIC;
  rule_message.rtm_scope = RT_SCOPE_UNIVERSE;
  rule_message.rtm_table = RT_TABLE_UNSPEC;

  rule_message.rtm_flags |= FIB_RULE_FIND_SADDR;

  switch (verb) {
    case Verb::kAdd:
      if (!source_range.IsInitialized()) {
        QUIC_LOG(ERROR) << "Source range must be initialized.";
        return false;
      }
      operation = RtnetlinkMessage::Operation::NEW;
      flags |= NLM_F_EXCL | NLM_F_CREATE;
      rule_message.rtm_type = FRA_DST;
      rule_message.rtm_src_len = source_range.prefix_length();
      break;
    case Verb::kRemove:
      operation = RtnetlinkMessage::Operation::DEL;
      break;
    case Verb::kReplace:
      QUIC_LOG(ERROR) << "Unsupported verb: kReplace";
      return false;
  }
  auto message =
      RuleMessage::New(operation, flags, seq_, getpid(), &rule_message);

  message.AppendAttribute(RTA_TABLE, &table, sizeof(table));

  if (source_range.IsInitialized()) {
    std::string packed_src = source_range.prefix().ToPackedString();
    message.AppendAttribute(RTA_SRC,
                            reinterpret_cast<const void*>(packed_src.c_str()),
                            packed_src.size());
  }

  if (!Send(message.BuildIoVec().get(), message.IoVecSize())) {
    QUIC_LOG(ERROR) << "send failed";
    return false;
  }

  UnknownParser parser;
  if (!Recv(seq_++, &parser)) {
    QUIC_LOG(ERROR) << "receive failed.";
    return false;
  }
  return true;
}

bool Netlink::Send(struct iovec* iov, size_t iovlen) {
  if (!OpenSocket()) {
    QUIC_LOG(ERROR) << "can't open socket";
    return false;
  }

  // an address for communicating with the kernel netlink code
  sockaddr_nl netlink_address;
  memset(&netlink_address, 0, sizeof(netlink_address));
  netlink_address.nl_family = AF_NETLINK;
  netlink_address.nl_pid = 0;     // destination is kernel
  netlink_address.nl_groups = 0;  // no multicast

  struct msghdr msg = {
      &netlink_address, sizeof(netlink_address), iov, iovlen, nullptr, 0, 0};

  if (kernel_->sendmsg(socket_fd_, &msg, 0) < 0) {
    QUIC_LOG(ERROR) << "sendmsg failed";
    CloseSocket();
    return false;
  }

  return true;
}

bool Netlink::Recv(uint32_t seq, NetlinkParserInterface* parser) {
  sockaddr_nl netlink_address;

  // replies can span multiple packets
  for (;;) {
    socklen_t address_length = sizeof(netlink_address);

    // First, call recvfrom with buffer size of 0 and MSG_PEEK | MSG_TRUNC set
    // so that we know the size of the incoming packet before actually receiving
    // it.
    int next_packet_size = kernel_->recvfrom(
        socket_fd_, recvbuf_.get(), /* len = */ 0, MSG_PEEK | MSG_TRUNC,
        reinterpret_cast<struct sockaddr*>(&netlink_address), &address_length);
    if (next_packet_size < 0) {
      QUIC_LOG(ERROR)
          << "error recvfrom with MSG_PEEK | MSG_TRUNC to get packet length.";
      CloseSocket();
      return false;
    }
    QUIC_VLOG(3) << "netlink packet size: " << next_packet_size;
    if (next_packet_size > recvbuf_length_) {
      QUIC_VLOG(2) << "resizing recvbuf to " << next_packet_size;
      ResetRecvBuf(next_packet_size);
    }

    // Get the packet for real.
    memset(recvbuf_.get(), 0, recvbuf_length_);
    int len = kernel_->recvfrom(
        socket_fd_, recvbuf_.get(), recvbuf_length_, /* flags = */ 0,
        reinterpret_cast<struct sockaddr*>(&netlink_address), &address_length);
    QUIC_VLOG(3) << "recvfrom returned: " << len;
    if (len < 0) {
      QUIC_LOG(INFO) << "can't receive netlink packet";
      CloseSocket();
      return false;
    }

    // there may be multiple nlmsg's in each reply packet
    struct nlmsghdr* netlink_message;
    for (netlink_message = reinterpret_cast<struct nlmsghdr*>(recvbuf_.get());
         NLMSG_OK(netlink_message, len);
         netlink_message = NLMSG_NEXT(netlink_message, len)) {
      QUIC_VLOG(3) << "netlink_message->nlmsg_type = "
                   << netlink_message->nlmsg_type;
      // make sure this is to us
      if (netlink_message->nlmsg_seq != seq) {
        QUIC_LOG(INFO) << "netlink_message not meant for us."
                       << " seq: " << seq
                       << " nlmsg_seq: " << netlink_message->nlmsg_seq;
        continue;
      }

      // done with this whole reply (not just this particular packet)
      if (netlink_message->nlmsg_type == NLMSG_DONE) {
        return true;
      }
      if (netlink_message->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr* err =
            reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(netlink_message));
        if (netlink_message->nlmsg_len <
            NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
          QUIC_LOG(INFO) << "netlink_message ERROR truncated";
        } else {
          // an ACK
          if (err->error == 0) {
            QUIC_VLOG(3) << "Netlink sent an ACK";
            return true;
          }
          QUIC_LOG(INFO) << "netlink_message ERROR: " << err->error;
        }
        return false;
      }

      parser->Run(netlink_message);
    }
  }
}

}  // namespace quic
