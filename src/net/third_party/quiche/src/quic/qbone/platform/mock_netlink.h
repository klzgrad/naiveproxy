// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_MOCK_NETLINK_H_
#define QUICHE_QUIC_QBONE_PLATFORM_MOCK_NETLINK_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/platform/netlink_interface.h"

namespace quic {

class MockNetlink : public NetlinkInterface {
 public:
  MOCK_METHOD2(GetLinkInfo, bool(const string&, LinkInfo*));

  MOCK_METHOD4(GetAddresses,
               bool(int, uint8_t, std::vector<AddressInfo>*, int*));

  MOCK_METHOD7(ChangeLocalAddress,
               bool(uint32_t,
                    Verb,
                    const QuicIpAddress&,
                    uint8_t,
                    uint8_t,
                    uint8_t,
                    const std::vector<struct rtattr*>&));

  MOCK_METHOD1(GetRouteInfo, bool(std::vector<RoutingRule>*));

  MOCK_METHOD6(
      ChangeRoute,
      bool(Verb, uint32_t, const IpRange&, uint8_t, QuicIpAddress, int32_t));

  MOCK_METHOD1(GetRuleInfo, bool(std::vector<IpRule>*));

  MOCK_METHOD3(ChangeRule, bool(Verb, uint32_t, IpRange));

  MOCK_METHOD2(Send, bool(struct iovec*, size_t));

  MOCK_METHOD2(Recv, bool(uint32_t, NetlinkParserInterface*));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_MOCK_NETLINK_H_
