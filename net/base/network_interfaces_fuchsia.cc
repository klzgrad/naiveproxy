// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_interfaces.h"

#include <netstack/cpp/fidl.h>

#include "base/fuchsia/component_context.h"
#include "net/base/ip_endpoint.h"

namespace net {

IPAddress NetAddressToIPAddress(const netstack::NetAddress& addr) {
  if (addr.ipv4) {
    return IPAddress(addr.ipv4->addr.data(), addr.ipv4->addr.count());
  }
  if (addr.ipv6) {
    return IPAddress(addr.ipv6->addr.data(), addr.ipv6->addr.count());
  }
  return IPAddress();
}

bool GetNetworkList(NetworkInterfaceList* networks, int policy) {
  netstack::NetstackSyncPtr netstack =
      base::fuchsia::ComponentContext::GetDefault()
          ->ConnectToServiceSync<netstack::Netstack>();

  fidl::VectorPtr<netstack::NetInterface> interfaces;
  if (!netstack->GetInterfaces(&interfaces))
    return false;

  for (auto& interface : interfaces.get()) {
    // Check if the interface is up.
    if (!(interface.flags & netstack::NetInterfaceFlagUp))
      continue;

    // Skip loopback.
    if (interface.features & netstack::interfaceFeatureLoopback)
      continue;

    NetworkChangeNotifier::ConnectionType connection_type =
        (interface.features & netstack::interfaceFeatureWlan)
            ? NetworkChangeNotifier::CONNECTION_WIFI
            : NetworkChangeNotifier::CONNECTION_UNKNOWN;

    // TODO(sergeyu): attributes field is used to return address state for IPv6
    // addresses. Currently Netstack doesn't provide this information.
    int attributes = 0;

    networks->push_back(NetworkInterface(
        *interface.name, *interface.name, interface.id, connection_type,
        NetAddressToIPAddress(interface.addr),
        MaskPrefixLength(NetAddressToIPAddress(interface.netmask)),
        attributes));
  }

  return true;
}

std::string GetWifiSSID() {
  NOTIMPLEMENTED();
  return std::string();
}

}  // namespace net
