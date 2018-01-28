// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NQE_NETWORK_ID_H_
#define NET_NQE_NETWORK_ID_H_

#include <string>
#include <tuple>

#include "base/strings/string_number_conversions.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"

namespace {

const char kValueSeparator[] = ",";

// Parses |connection_type_string| as a NetworkChangeNotifier::ConnectionType.
// |connection_type_string| must contain the
// NetworkChangeNotifier::ConnectionType enum as an interger.
net::NetworkChangeNotifier::ConnectionType ConvertStringToConnectionType(
    const std::string& connection_type_string) {
  int connection_type_int =
      static_cast<int>(net::NetworkChangeNotifier::CONNECTION_UNKNOWN);
  bool connection_type_available =
      base::StringToInt(connection_type_string, &connection_type_int);

  if (!connection_type_available || connection_type_int < 0 ||
      connection_type_int >
          static_cast<int>(net::NetworkChangeNotifier::CONNECTION_LAST)) {
    DCHECK(false);
    return net::NetworkChangeNotifier::CONNECTION_UNKNOWN;
  }
  return static_cast<net::NetworkChangeNotifier::ConnectionType>(
      connection_type_int);
}

}  // namespace

namespace net {
namespace nqe {
namespace internal {

// NetworkID is used to uniquely identify a network.
// For the purpose of network quality estimation and caching, a network is
// uniquely identified by a combination of |type| and
// |id|. This approach is unable to distinguish networks with
// same name (e.g., different Wi-Fi networks with same SSID).
// This is a protected member to expose it to tests.
struct NET_EXPORT_PRIVATE NetworkID {
  static NetworkID FromString(const std::string& network_id) {
    size_t separator_index = network_id.find(kValueSeparator);
    DCHECK_NE(std::string::npos, separator_index);
    if (separator_index == std::string::npos) {
      return NetworkID(NetworkChangeNotifier::CONNECTION_UNKNOWN,
                       std::string());
    }

    return NetworkID(
        ConvertStringToConnectionType(network_id.substr(separator_index + 1)),
        network_id.substr(0, separator_index));
  }
  NetworkID(NetworkChangeNotifier::ConnectionType type, const std::string& id)
      : type(type), id(id) {}
  NetworkID(const NetworkID& other) : type(other.type), id(other.id) {}
  ~NetworkID() {}

  bool operator==(const NetworkID& other) const {
    return type == other.type && id == other.id;
  }

  bool operator!=(const NetworkID& other) const { return !operator==(other); }

  NetworkID& operator=(const NetworkID& other) {
    type = other.type;
    id = other.id;
    return *this;
  }

  // Overloaded to support ordered collections.
  bool operator<(const NetworkID& other) const {
    return std::tie(type, id) < std::tie(other.type, other.id);
  }

  std::string ToString() const {
    return id + kValueSeparator + base::IntToString(static_cast<int>(type));
  }

  // Connection type of the network.
  NetworkChangeNotifier::ConnectionType type;

  // Name of this network. This is set to:
  // - Wi-Fi SSID if the device is connected to a Wi-Fi access point and the
  //   SSID name is available, or
  // - MCC/MNC code of the cellular carrier if the device is connected to a
  //   cellular network, or
  // - "Ethernet" in case the device is connected to ethernet.
  // - An empty string in all other cases or if the network name is not
  //   exposed by platform APIs.
  std::string id;
};

}  // namespace internal
}  // namespace nqe
}  // namespace net

#endif  // NET_NQE_NETWORK_ID_H_
