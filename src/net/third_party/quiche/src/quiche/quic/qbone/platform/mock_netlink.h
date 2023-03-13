// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_MOCK_NETLINK_H_
#define QUICHE_QUIC_QBONE_PLATFORM_MOCK_NETLINK_H_

#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/platform/netlink_interface.h"

namespace quic {

class MockNetlink : public NetlinkInterface {
 public:
  MOCK_METHOD(bool, GetLinkInfo, (const std::string&, LinkInfo*), (override));

  MOCK_METHOD(bool, GetAddresses,
              (int, uint8_t, std::vector<AddressInfo>*, int*), (override));

  MOCK_METHOD(bool, ChangeLocalAddress,
              (uint32_t, Verb, const QuicIpAddress&, uint8_t, uint8_t, uint8_t,
               const std::vector<struct rtattr*>&),
              (override));

  MOCK_METHOD(bool, GetRouteInfo, (std::vector<RoutingRule>*), (override));

  MOCK_METHOD(bool, ChangeRoute,
              (Verb, uint32_t, const IpRange&, uint8_t, QuicIpAddress, int32_t,
               uint32_t),
              (override));

  MOCK_METHOD(bool, GetRuleInfo, (std::vector<IpRule>*), (override));

  MOCK_METHOD(bool, ChangeRule, (Verb, uint32_t, IpRange), (override));

  MOCK_METHOD(bool, Send, (struct iovec*, size_t), (override));

  MOCK_METHOD(bool, Recv, (uint32_t, NetlinkParserInterface*), (override));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_MOCK_NETLINK_H_
