// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_interfaces.h"

#include <netstack/netconfig.h>

#include "net/base/ip_endpoint.h"
#include "net/base/network_interfaces_posix.h"

namespace net {

bool GetNetworkList(NetworkInterfaceList* networks, int policy) {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s <= 0) {
    PLOG(ERROR) << "socket";
    return false;
  }

  netc_get_if_info_t netconfig;
  int size = ioctl_netc_get_if_info(s, &netconfig);
  PCHECK(close(s) == 0);

  if (size < 0) {
    PLOG(ERROR) << "ioctl_netc_get_if_info";
    return false;
  }

  networks->clear();

  for (size_t i = 0; i < netconfig.n_info; ++i) {
    netc_if_info_t* interface = netconfig.info + i;

    // Skip loopback addresses.
    if (internal::IsLoopbackOrUnspecifiedAddress(
            reinterpret_cast<sockaddr*>(&(interface->addr)))) {
      continue;
    }

    IPEndPoint address;
    if (!address.FromSockAddr(reinterpret_cast<sockaddr*>(&(interface->addr)),
                              sizeof(interface->addr))) {
      DLOG(WARNING) << "ioctl_netc_get_if_info returned invalid address.";
      continue;
    }

    int prefix_length = 0;
    IPEndPoint netmask;
    if (netmask.FromSockAddr(reinterpret_cast<sockaddr*>(&(interface->netmask)),
                             sizeof(interface->netmask))) {
      prefix_length = MaskPrefixLength(netmask.address());
    }

    // TODO(sergeyu): attributes field is used to return address state for IPv6
    // addresses. Currently ioctl_netc_get_if_info doesn't provide this
    // information.
    int attributes = 0;

    networks->push_back(
        NetworkInterface(interface->name, interface->name, interface->index,
                         NetworkChangeNotifier::CONNECTION_UNKNOWN,
                         address.address(), prefix_length, attributes));
  }

  return true;
}

std::string GetWifiSSID() {
  NOTIMPLEMENTED();
  return std::string();
}

}  // namespace net
