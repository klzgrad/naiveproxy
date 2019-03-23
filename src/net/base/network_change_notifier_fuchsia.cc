// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_change_notifier_fuchsia.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/fuchsia/component_context.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "net/base/network_interfaces.h"
#include "net/base/network_interfaces_fuchsia.h"

namespace net {
namespace {

using ConnectionType = NetworkChangeNotifier::ConnectionType;

// Adapts a base::RepeatingCallback to a std::function object.
// Useful when binding callbacks to asynchronous FIDL calls, because
// it allows the caller to reference in-scope move-only objects as well as use
// Chromium's ownership signifiers such as base::Passed, base::Unretained, etc.
//
// Note that the function takes a RepeatingCallback because it is copyable, but
// in practice the callback will only be executed once by the FIDL system.
template <typename R, typename... Args>
std::function<R(Args...)> WrapCallbackAsFunction(
    base::RepeatingCallback<R(Args...)> callback) {
  return
      [callback](Args... args) { callback.Run(std::forward<Args>(args)...); };
}

}  // namespace

NetworkChangeNotifierFuchsia::NetworkChangeNotifierFuchsia(
    uint32_t required_features)
    : NetworkChangeNotifierFuchsia(
          base::fuchsia::ComponentContext::GetDefault()
              ->ConnectToService<fuchsia::netstack::Netstack>(),
          required_features) {}

NetworkChangeNotifierFuchsia::NetworkChangeNotifierFuchsia(
    fuchsia::netstack::NetstackPtr netstack,
    uint32_t required_features)
    : netstack_(std::move(netstack)), required_features_(required_features) {
  DCHECK(netstack_);

  netstack_.set_error_handler([](zx_status_t status) {
    ZX_LOG(ERROR, status) << "Lost connection to netstack.";
  });
  netstack_.events().OnInterfacesChanged =
      [this](std::vector<fuchsia::netstack::NetInterface> interfaces) {
        ProcessInterfaceList(base::OnceClosure(), std::move(interfaces));
      };

  // Fetch the interface list synchronously, so that an initial ConnectionType
  // is available before we return.
  base::RunLoop wait_for_interfaces;
  netstack_->GetInterfaces(
      [this, quit_closure = wait_for_interfaces.QuitClosure()](
          std::vector<fuchsia::netstack::NetInterface> interfaces) {
        ProcessInterfaceList(quit_closure, std::move(interfaces));
      });
  wait_for_interfaces.Run();
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
    base::OnceClosure on_initialized_cb,
    std::vector<fuchsia::netstack::NetInterface> interfaces) {
  netstack_->GetRouteTable(WrapCallbackAsFunction(base::BindRepeating(
      &NetworkChangeNotifierFuchsia::OnRouteTableReceived,
      base::Unretained(this), base::Passed(std::move(on_initialized_cb)),
      base::Passed(std::move(interfaces)))));
}

void NetworkChangeNotifierFuchsia::OnRouteTableReceived(
    base::OnceClosure on_initialized_cb,
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

  // TODO(https://crbug.com/848355): Treat SSID changes as IP address changes.

  if (addresses != cached_addresses_) {
    std::swap(cached_addresses_, addresses);
    if (on_initialized_cb.is_null()) {
      NotifyObserversOfIPAddressChange();
    }
    connection_type_changed = true;
  }

  if (on_initialized_cb.is_null() && connection_type_changed) {
    NotifyObserversOfConnectionTypeChange();
  }

  if (!on_initialized_cb.is_null()) {
    std::move(on_initialized_cb).Run();
  }
}

}  // namespace net
