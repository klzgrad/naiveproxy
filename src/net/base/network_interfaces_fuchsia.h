// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_INTERFACES_FUCHSIA_H_
#define NET_BASE_NETWORK_INTERFACES_FUCHSIA_H_

#include <vector>

namespace fuchsia {
namespace netstack {
class NetAddress;
class NetInterface;
}  // namespace netstack
}  // namespace fuchsia

namespace net {

class IPAddress;
struct NetworkInterface;

namespace internal {

// Converts a Fuchsia Netstack NetInterface object to NetworkInterface objects.
// Interfaces with more than one IPv6 address will yield multiple
// NetworkInterface objects, with friendly names to distinguish the different
// IPs (e.g. "wlan" with three IPv6 IPs yields wlan-0, wlan-1, wlan-2).
std::vector<NetworkInterface> NetInterfaceToNetworkInterfaces(
    const fuchsia::netstack::NetInterface& iface_in);

// Converts a Fuchsia IPv4/IPv6 address to a Chromium IPAddress.
IPAddress NetAddressToIPAddress(const fuchsia::netstack::NetAddress& addr);

}  // namespace internal
}  // namespace net

#endif  // NET_BASE_NETWORK_INTERFACES_FUCHSIA_H_
