// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/qbone/bonnet/tun_device_controller.h"

#include <linux/if_addr.h>
#include <linux/rtnetlink.h>

#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_test.h"
#include "quic/qbone/platform/mock_netlink.h"
#include "quic/qbone/qbone_constants.h"

ABSL_DECLARE_FLAG(bool, qbone_tun_device_replace_default_routing_rules);

namespace quic {
namespace {
using ::testing::Eq;

constexpr int kIfindex = 42;
constexpr char kIfname[] = "qbone0";

const IpRange kIpRange = []() {
  IpRange range;
  QCHECK(range.FromString("2604:31c0:2::/64"));
  return range;
}();

constexpr char kOldAddress[] = "1.2.3.4";
constexpr int kOldPrefixLen = 24;

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

MATCHER_P(IpRangeEq, range,
          absl::StrCat("expected IpRange to equal ", range.ToString())) {
  return arg == range;
}

class TunDeviceControllerTest : public QuicTest {
 public:
  TunDeviceControllerTest()
      : controller_(kIfname, true, &netlink_),
        link_local_range_(
            *QboneConstants::TerminatorLocalAddressRange()) {}

 protected:
  void ExpectLinkInfo(const std::string& interface_name, int ifindex) {
    EXPECT_CALL(netlink_, GetLinkInfo(interface_name, _))
        .WillOnce(
            Invoke([ifindex](absl::string_view ifname,
                             NetlinkInterface::LinkInfo* link_info) {
              link_info->index = ifindex;
              return true;
            }));
  }

  MockNetlink netlink_;
  TunDeviceController controller_;

  IpRange link_local_range_;
};

TEST_F(TunDeviceControllerTest, AddressAppliedWhenNoneExisted) {
  ExpectLinkInfo(kIfname, kIfindex);

  EXPECT_CALL(netlink_, GetAddresses(kIfindex, _, _, _)).WillOnce(Return(true));

  EXPECT_CALL(netlink_,
              ChangeLocalAddress(
                  kIfindex, NetlinkInterface::Verb::kAdd,
                  kIpRange.FirstAddressInRange(), kIpRange.prefix_length(),
                  IFA_F_PERMANENT | IFA_F_NODAD, RT_SCOPE_LINK, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(controller_.UpdateAddress(kIpRange));
}

TEST_F(TunDeviceControllerTest, OldAddressesAreRemoved) {
  ExpectLinkInfo(kIfname, kIfindex);

  EXPECT_CALL(netlink_, GetAddresses(kIfindex, _, _, _))
      .WillOnce(
          Invoke([](int interface_index, uint8_t unwanted_flags,
                    std::vector<NetlinkInterface::AddressInfo>* addresses,
                    int* num_ipv6_nodad_dadfailed_addresses) {
            NetlinkInterface::AddressInfo info{};
            info.interface_address.FromString(kOldAddress);
            info.prefix_length = kOldPrefixLen;
            addresses->emplace_back(info);
            return true;
          }));

  QuicIpAddress old_address;
  old_address.FromString(kOldAddress);

  EXPECT_CALL(netlink_, ChangeLocalAddress(
                            kIfindex, NetlinkInterface::Verb::kRemove,
                            old_address, kOldPrefixLen, _, _, _))
      .WillOnce(Return(true));

  EXPECT_CALL(netlink_,
              ChangeLocalAddress(
                  kIfindex, NetlinkInterface::Verb::kAdd,
                  kIpRange.FirstAddressInRange(), kIpRange.prefix_length(),
                  IFA_F_PERMANENT | IFA_F_NODAD, RT_SCOPE_LINK, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(controller_.UpdateAddress(kIpRange));
}

TEST_F(TunDeviceControllerTest, UpdateRoutesRemovedOldRoutes) {
  ExpectLinkInfo(kIfname, kIfindex);

  const int num_matching_routes = 3;
  EXPECT_CALL(netlink_, GetRouteInfo(_))
      .WillOnce(Invoke(
          [](std::vector<NetlinkInterface::RoutingRule>* routing_rules) {
            NetlinkInterface::RoutingRule non_matching_route;
            non_matching_route.table = QboneConstants::kQboneRouteTableId;
            non_matching_route.out_interface = kIfindex + 1;
            routing_rules->push_back(non_matching_route);

            NetlinkInterface::RoutingRule matching_route;
            matching_route.table = QboneConstants::kQboneRouteTableId;
            matching_route.out_interface = kIfindex;
            for (int i = 0; i < num_matching_routes; i++) {
              routing_rules->push_back(matching_route);
            }

            NetlinkInterface::RoutingRule non_matching_table;
            non_matching_table.table =
                QboneConstants::kQboneRouteTableId + 1;
            non_matching_table.out_interface = kIfindex;
            routing_rules->push_back(non_matching_table);
            return true;
          }));

  EXPECT_CALL(netlink_, ChangeRoute(NetlinkInterface::Verb::kRemove,
                                    QboneConstants::kQboneRouteTableId, _,
                                    _, _, kIfindex))
      .Times(num_matching_routes)
      .WillRepeatedly(Return(true));

  EXPECT_CALL(netlink_, GetRuleInfo(_)).WillOnce(Return(true));

  EXPECT_CALL(netlink_, ChangeRule(NetlinkInterface::Verb::kAdd,
                                   QboneConstants::kQboneRouteTableId,
                                   IpRangeEq(kIpRange)))
      .WillOnce(Return(true));

  EXPECT_CALL(netlink_,
              ChangeRoute(NetlinkInterface::Verb::kReplace,
                          QboneConstants::kQboneRouteTableId,
                          IpRangeEq(link_local_range_), _, _, kIfindex))
      .WillOnce(Return(true));

  EXPECT_TRUE(controller_.UpdateRoutes(kIpRange, {}));
}

TEST_F(TunDeviceControllerTest, UpdateRoutesAddsNewRoutes) {
  ExpectLinkInfo(kIfname, kIfindex);

  EXPECT_CALL(netlink_, GetRouteInfo(_)).WillOnce(Return(true));

  EXPECT_CALL(netlink_, GetRuleInfo(_)).WillOnce(Return(true));

  EXPECT_CALL(netlink_, ChangeRoute(NetlinkInterface::Verb::kReplace,
                                    QboneConstants::kQboneRouteTableId,
                                    IpRangeEq(kIpRange), _, _, kIfindex))
      .Times(2)
      .WillRepeatedly(Return(true))
      .RetiresOnSaturation();

  EXPECT_CALL(netlink_, ChangeRule(NetlinkInterface::Verb::kAdd,
                                   QboneConstants::kQboneRouteTableId,
                                   IpRangeEq(kIpRange)))
      .WillOnce(Return(true));

  EXPECT_CALL(netlink_,
              ChangeRoute(NetlinkInterface::Verb::kReplace,
                          QboneConstants::kQboneRouteTableId,
                          IpRangeEq(link_local_range_), _, _, kIfindex))
      .WillOnce(Return(true));

  EXPECT_TRUE(controller_.UpdateRoutes(kIpRange, {kIpRange, kIpRange}));
}

TEST_F(TunDeviceControllerTest, EmptyUpdateRouteKeepsLinkLocalRoute) {
  ExpectLinkInfo(kIfname, kIfindex);

  EXPECT_CALL(netlink_, GetRouteInfo(_)).WillOnce(Return(true));

  EXPECT_CALL(netlink_, GetRuleInfo(_)).WillOnce(Return(true));

  EXPECT_CALL(netlink_, ChangeRule(NetlinkInterface::Verb::kAdd,
                                   QboneConstants::kQboneRouteTableId,
                                   IpRangeEq(kIpRange)))
      .WillOnce(Return(true));

  EXPECT_CALL(netlink_,
              ChangeRoute(NetlinkInterface::Verb::kReplace,
                          QboneConstants::kQboneRouteTableId,
                          IpRangeEq(link_local_range_), _, _, kIfindex))
      .WillOnce(Return(true));

  EXPECT_TRUE(controller_.UpdateRoutes(kIpRange, {}));
}

TEST_F(TunDeviceControllerTest, DisablingRoutingRulesSkipsRuleCreation) {
  absl::SetFlag(&FLAGS_qbone_tun_device_replace_default_routing_rules, false);
  ExpectLinkInfo(kIfname, kIfindex);

  EXPECT_CALL(netlink_, GetRouteInfo(_)).WillOnce(Return(true));

  EXPECT_CALL(netlink_, ChangeRoute(NetlinkInterface::Verb::kReplace,
                                    QboneConstants::kQboneRouteTableId,
                                    IpRangeEq(kIpRange), _, _, kIfindex))
      .Times(2)
      .WillRepeatedly(Return(true))
      .RetiresOnSaturation();

  EXPECT_CALL(netlink_,
              ChangeRoute(NetlinkInterface::Verb::kReplace,
                          QboneConstants::kQboneRouteTableId,
                          IpRangeEq(link_local_range_), _, _, kIfindex))
      .WillOnce(Return(true));

  EXPECT_TRUE(controller_.UpdateRoutes(kIpRange, {kIpRange, kIpRange}));
}

class DisabledTunDeviceControllerTest : public QuicTest {
 public:
  DisabledTunDeviceControllerTest()
      : controller_(kIfname, false, &netlink_),
        link_local_range_(
            *QboneConstants::TerminatorLocalAddressRange()) {}

  StrictMock<MockNetlink> netlink_;
  TunDeviceController controller_;

  IpRange link_local_range_;
};

TEST_F(DisabledTunDeviceControllerTest, UpdateRoutesIsNop) {
  EXPECT_THAT(controller_.UpdateRoutes(kIpRange, {}), Eq(true));
}

TEST_F(DisabledTunDeviceControllerTest, UpdateAddressIsNop) {
  EXPECT_THAT(controller_.UpdateAddress(kIpRange), Eq(true));
}

}  // namespace
}  // namespace quic
