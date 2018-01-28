// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_id.h"

#include <tuple>

#include "base/base64.h"
#include "base/strings/string_number_conversions.h"
#include "net/nqe/proto/network_id_proto.pb.h"

namespace net {
namespace nqe {
namespace internal {

// static
NetworkID NetworkID::FromString(const std::string& network_id) {
  std::string base64_decoded;
  if (!base::Base64Decode(network_id, &base64_decoded))
    return NetworkID(NetworkChangeNotifier::CONNECTION_UNKNOWN, std::string());

  NetworkIDProto network_id_proto;
  if (!network_id_proto.ParseFromString(base64_decoded))
    return NetworkID(NetworkChangeNotifier::CONNECTION_UNKNOWN, std::string());

  return NetworkID(static_cast<NetworkChangeNotifier::ConnectionType>(
                       network_id_proto.connection_type()),
                   network_id_proto.id());
}

NetworkID::NetworkID(NetworkChangeNotifier::ConnectionType type,
                     const std::string& id)
    : type(type), id(id) {}

NetworkID::NetworkID(const NetworkID& other) = default;

NetworkID::~NetworkID() = default;

bool NetworkID::operator==(const NetworkID& other) const {
  return type == other.type && id == other.id;
}

bool NetworkID::operator!=(const NetworkID& other) const {
  return !operator==(other);
}

NetworkID& NetworkID::operator=(const NetworkID& other) = default;

// Overloaded to support ordered collections.
bool NetworkID::operator<(const NetworkID& other) const {
  return std::tie(type, id) < std::tie(other.type, other.id);
}

std::string NetworkID::ToString() const {
  NetworkIDProto network_id_proto;
  network_id_proto.set_connection_type(static_cast<int>(type));
  network_id_proto.set_id(id);

  std::string serialized_network_id;
  if (!network_id_proto.SerializeToString(&serialized_network_id))
    return "";

  std::string base64_encoded;
  base::Base64Encode(serialized_network_id, &base64_encoded);

  return base64_encoded;
}

}  // namespace internal
}  // namespace nqe
}  // namespace net
