// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_change_notifier_fuchsia.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/fuchsia/service_directory_client.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "net/base/network_interfaces.h"
#include "net/base/network_interfaces_fuchsia.h"

namespace net {

NetworkChangeNotifierFuchsia::NetworkChangeNotifierFuchsia(
    uint32_t required_features)
    : NetworkChangeNotifierFuchsia(
          base::fuchsia::ServiceDirectoryClient::ForCurrentProcess()
              ->ConnectToService<fuchsia::netstack::Netstack>(),
          required_features) {}

NetworkChangeNotifierFuchsia::NetworkChangeNotifierFuchsia(
    fuchsia::netstack::NetstackPtr netstack,
    uint32_t required_features)
    : required_features_(required_features), netstack_(std::move(netstack)) {
  DCHECK(netstack_);

  netstack_.set_error_handler([](zx_status_t status) {
    // TODO(https://crbug.com/901092): Unit tests that use NetworkService are
    // crashing because NetworkService does not clean up properly, and the
    // netstack connection is cancelled, causing this fatal error.
    // When the NetworkService cleanup is fixed, we should make this log FATAL.
    ZX_LOG(ERROR, status) << "Lost connection to netstack.";
  });
  netstack_.events().OnInterfacesChanged = fit::bind_member(
      this, &NetworkChangeNotifierFuchsia::ProcessInterfaceList);

  // Manually fetch the interface list, on which to base an initial
  // ConnectionType.
  netstack_->GetInterfaces(fit::bind_member(
      this, &NetworkChangeNotifierFuchsia::ProcessInterfaceList));
}

NetworkChangeNotifierFuchsia::~NetworkChangeNotifierFuchsia() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

NetworkChangeNotifier::ConnectionType
NetworkChangeNotifierFuchsia::GetCurrentConnectionType() const {
  ConnectionType type = static_cast<ConnectionType>(
      base::subtle::Acquire_Load(&cached_connection_type_));
  return type;
}

void NetworkChangeNotifierFuchsia::ProcessInterfaceList(
    std::vector<fuchsia::netstack::NetInterface> interfaces) {
  netstack_->GetRouteTable(
      [this, interfaces = std::move(interfaces)](
          std::vector<fuchsia::netstack::RouteTableEntry> route_table) mutable {
        OnRouteTableReceived(std::move(interfaces), std::move(route_table));
      });
}

void NetworkChangeNotifierFuchsia::OnRouteTableReceived(
    std::vector<fuchsia::netstack::NetInterface> interfaces,
    std::vector<fuchsia::netstack::RouteTableEntry> route_table) {
  // Create a set of NICs that have default routes (ie 0.0.0.0).
  base::flat_set<uint32_t> default_route_ids;
  for (const auto& route : route_table) {
    if (MaskPrefixLength(
            internal::FuchsiaIpAddressToIPAddress(route.netmask)) == 0) {
      default_route_ids.insert(route.nicid);
    }
  }

  ConnectionType connection_type = CONNECTION_NONE;
  base::flat_set<IPAddress> addresses;
  for (auto& interface : interfaces) {
    // Filter out loopback and invalid connection types.
    if ((internal::ConvertConnectionType(interface) ==
         NetworkChangeNotifier::CONNECTION_NONE) ||
        (interface.features &
         fuchsia::hardware::ethernet::INFO_FEATURE_LOOPBACK)) {
      continue;
    }

    // Filter out interfaces that do not meet the |required_features_|.
    if ((interface.features & required_features_) != required_features_) {
      continue;
    }

    // Filter out interfaces with non-default routes.
    if (!default_route_ids.contains(interface.id)) {
      continue;
    }

    std::vector<NetworkInterface> flattened_interfaces =
        internal::NetInterfaceToNetworkInterfaces(interface);
    if (flattened_interfaces.empty()) {
      continue;
    }

    // Add the addresses from this interface to the list of all addresses.
    std::transform(
        flattened_interfaces.begin(), flattened_interfaces.end(),
        std::inserter(addresses, addresses.begin()),
        [](const NetworkInterface& interface) { return interface.address; });

    // Set the default connection to the first interface connection found.
    if (connection_type == CONNECTION_NONE) {
      connection_type = flattened_interfaces.front().type;
    }
  }

  bool connection_type_changed = false;
  if (connection_type != cached_connection_type_) {
    base::subtle::Release_Store(&cached_connection_type_, connection_type);
    connection_type_changed = true;
  }

  if (addresses != cached_addresses_) {
    std::swap(cached_addresses_, addresses);
    NotifyObserversOfIPAddressChange();
    connection_type_changed = true;
  }

  if (connection_type_changed) {
    NotifyObserversOfConnectionTypeChange();
  }
}

}  // namespace net
