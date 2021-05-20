// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_session_key.h"

#include "base/feature_list.h"
#include "net/base/features.h"

namespace net {

QuicSessionKey::QuicSessionKey() = default;

QuicSessionKey::QuicSessionKey(const HostPortPair& host_port_pair,
                               PrivacyMode privacy_mode,
                               const SocketTag& socket_tag,
                               const NetworkIsolationKey& network_isolation_key,
                               bool disable_secure_dns)
    : QuicSessionKey(host_port_pair.host(),
                     host_port_pair.port(),
                     privacy_mode,
                     socket_tag,
                     network_isolation_key,
                     disable_secure_dns) {}

QuicSessionKey::QuicSessionKey(const std::string& host,
                               uint16_t port,
                               PrivacyMode privacy_mode,
                               const SocketTag& socket_tag,
                               const NetworkIsolationKey& network_isolation_key,
                               bool disable_secure_dns)
    : QuicSessionKey(
          // TODO(crbug.com/1103350): Handle non-boolean privacy modes.
          quic::QuicServerId(
              host,
              port,
              privacy_mode == PRIVACY_MODE_ENABLED_WITHOUT_CLIENT_CERTS ||
                  privacy_mode == PRIVACY_MODE_ENABLED),
          socket_tag,
          network_isolation_key,
          disable_secure_dns) {}

QuicSessionKey::QuicSessionKey(const quic::QuicServerId& server_id,
                               const SocketTag& socket_tag,
                               const NetworkIsolationKey& network_isolation_key,
                               bool disable_secure_dns)
    : server_id_(server_id),
      socket_tag_(socket_tag),
      network_isolation_key_(
          base::FeatureList::IsEnabled(
              features::kPartitionConnectionsByNetworkIsolationKey)
              ? network_isolation_key
              : NetworkIsolationKey()),
      disable_secure_dns_(disable_secure_dns) {}

QuicSessionKey::QuicSessionKey(const QuicSessionKey& other) = default;

bool QuicSessionKey::operator<(const QuicSessionKey& other) const {
  return std::tie(server_id_, socket_tag_, network_isolation_key_,
                  disable_secure_dns_) <
         std::tie(other.server_id_, other.socket_tag_,
                  other.network_isolation_key_, other.disable_secure_dns_);
}
bool QuicSessionKey::operator==(const QuicSessionKey& other) const {
  return server_id_ == other.server_id_ && socket_tag_ == other.socket_tag_ &&
         network_isolation_key_ == other.network_isolation_key_ &&
         disable_secure_dns_ == other.disable_secure_dns_;
}

bool QuicSessionKey::CanUseForAliasing(const QuicSessionKey& other) const {
  return server_id_.privacy_mode_enabled() ==
             other.server_id_.privacy_mode_enabled() &&
         socket_tag_ == other.socket_tag_ &&
         network_isolation_key_ == other.network_isolation_key_ &&
         disable_secure_dns_ == other.disable_secure_dns_;
}

size_t QuicSessionKey::EstimateMemoryUsage() const {
  return server_id_.EstimateMemoryUsage();
}

}  // namespace net
