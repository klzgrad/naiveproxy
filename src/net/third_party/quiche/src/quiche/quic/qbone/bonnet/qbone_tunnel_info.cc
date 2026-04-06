// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/bonnet/qbone_tunnel_info.h"

#include <vector>

namespace quic {

QuicIpAddress QboneTunnelInfo::GetAddress() {
  QuicIpAddress no_address;

  NetlinkInterface::LinkInfo link_info{};
  if (!netlink_->GetLinkInfo(ifname_, &link_info)) {
    return no_address;
  }

  std::vector<NetlinkInterface::AddressInfo> addresses;
  if (!netlink_->GetAddresses(link_info.index, 0, &addresses, nullptr)) {
    return no_address;
  }

  quic::QuicIpAddress link_local_subnet;
  if (!link_local_subnet.FromString("FE80::")) {
    return no_address;
  }

  for (const auto& address : addresses) {
    if (address.interface_address.IsInitialized() &&
        !link_local_subnet.InSameSubnet(address.interface_address, 10)) {
      return address.interface_address;
    }
  }

  return no_address;
}

}  // namespace quic
