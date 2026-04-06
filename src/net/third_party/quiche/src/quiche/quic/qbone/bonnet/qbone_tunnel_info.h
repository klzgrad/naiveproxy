// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_INFO_H_
#define QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_INFO_H_

#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/qbone/platform/netlink_interface.h"

namespace quic {

class QboneTunnelInfo {
 public:
  QboneTunnelInfo(std::string ifname, NetlinkInterface* netlink)
      : ifname_(std::move(ifname)), netlink_(netlink) {}

  // Returns the current QBONE tunnel address. Callers must use IsInitialized()
  // to ensure the returned address is valid.
  QuicIpAddress GetAddress();

 private:
  const std::string ifname_;
  NetlinkInterface* netlink_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_INFO_H_
