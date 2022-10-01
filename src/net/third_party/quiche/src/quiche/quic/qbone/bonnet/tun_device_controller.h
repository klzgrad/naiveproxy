// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_CONTROLLER_H_
#define QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_CONTROLLER_H_

#include "quiche/quic/qbone/bonnet/tun_device.h"
#include "quiche/quic/qbone/platform/netlink_interface.h"
#include "quiche/quic/qbone/qbone_control.pb.h"
#include "quiche/quic/qbone/qbone_control_stream.h"

namespace quic {

// TunDeviceController consumes control stream messages from a Qbone server
// and applies the given updates to the TUN device.
class TunDeviceController {
 public:
  // |ifname| is the interface name of the TUN device to be managed. This does
  // not take ownership of |netlink|.
  TunDeviceController(std::string ifname, bool setup_tun,
                      NetlinkInterface* netlink)
      : ifname_(std::move(ifname)), setup_tun_(setup_tun), netlink_(netlink) {}

  TunDeviceController(const TunDeviceController&) = delete;
  TunDeviceController& operator=(const TunDeviceController&) = delete;

  TunDeviceController(TunDeviceController&&) = delete;
  TunDeviceController& operator=(TunDeviceController&&) = delete;

  virtual ~TunDeviceController() = default;

  // Updates the local address of the TUN device to be the first address in the
  // given |response.ip_range()|.
  virtual bool UpdateAddress(const IpRange& desired_range);

  // Updates the set of routes that the TUN device will provide. All current
  // routes for the tunnel that do not exist in the |response| will be removed.
  virtual bool UpdateRoutes(const IpRange& desired_range,
                            const std::vector<IpRange>& desired_routes);

  // Same as UpdateRoutes, but will wait and retry up to the number of times
  // given by |retries| before giving up. This is an unpleasant workaround to
  // deal with older kernels that aren't always able to set a route with a
  // source address immediately after adding the address to the interface.
  //
  // TODO(b/179430548): Remove this once we've root-caused the underlying issue.
  virtual bool UpdateRoutesWithRetries(
      const IpRange& desired_range, const std::vector<IpRange>& desired_routes,
      int retries);

  virtual void RegisterAddressUpdateCallback(
      const std::function<void(QuicIpAddress)>& cb);

  virtual QuicIpAddress current_address();

 private:
  // Update the IP Rules, this should only be used by UpdateRoutes.
  bool UpdateRules(IpRange desired_range);

  const std::string ifname_;
  const bool setup_tun_;

  NetlinkInterface* netlink_;

  QuicIpAddress current_address_;

  std::vector<std::function<void(QuicIpAddress)>> address_update_cbs_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_CONTROLLER_H_
