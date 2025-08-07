// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_CONNECTION_ENDPOINT_METADATA_H_
#define NET_BASE_CONNECTION_ENDPOINT_METADATA_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "base/values.h"
#include "net/base/net_export.h"

namespace net {

// Metadata used to create UDP/TCP/TLS/etc connections or information about such
// a connection.
struct NET_EXPORT_PRIVATE ConnectionEndpointMetadata {
  // Expected to be parsed/consumed only by BoringSSL code and thus passed
  // around here only as a raw byte array.
  using EchConfigList = std::vector<uint8_t>;

  ConnectionEndpointMetadata();
  ConnectionEndpointMetadata(
      std::vector<std::string> supported_protocol_alpns,
      EchConfigList ech_config_list,
      std::string target_name,
      std::vector<std::vector<uint8_t>> trust_anchor_ids);
  ~ConnectionEndpointMetadata();

  ConnectionEndpointMetadata(const ConnectionEndpointMetadata&);
  ConnectionEndpointMetadata& operator=(const ConnectionEndpointMetadata&) =
      default;
  ConnectionEndpointMetadata(ConnectionEndpointMetadata&&);
  ConnectionEndpointMetadata& operator=(ConnectionEndpointMetadata&&) = default;

  bool operator==(const ConnectionEndpointMetadata& other) const = default;

  base::Value ToValue() const;
  static std::optional<ConnectionEndpointMetadata> FromValue(
      const base::Value& value);

  // ALPN strings for protocols supported by the endpoint. Empty for default
  // non-protocol endpoint.
  std::vector<std::string> supported_protocol_alpns;

  // If not empty, TLS Encrypted Client Hello config for the service.
  EchConfigList ech_config_list;

  // The target domain name of this metadata.
  std::string target_name;

  // A list of TLS Trust Anchor IDs advertised by the server, indicating
  // different options for trust anchors that it can offer. The client can
  // choose a subset of these to advertise in the TLS ClientHello to guide the
  // server as to which certificate it should serve so that the client will
  // trust it.
  std::vector<std::vector<uint8_t>> trust_anchor_ids;
};

}  // namespace net

#endif  // NET_BASE_CONNECTION_ENDPOINT_METADATA_H_
