// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_STREAM_KEY_H_
#define NET_HTTP_HTTP_STREAM_KEY_H_

#include <optional>
#include <string>

#include "base/values.h"
#include "net/base/net_export.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/privacy_mode.h"
#include "net/dns/public/secure_dns_policy.h"
#include "net/quic/quic_session_alias_key.h"
#include "net/quic/quic_session_key.h"
#include "net/socket/socket_tag.h"
#include "net/spdy/spdy_session_key.h"
#include "url/scheme_host_port.h"

namespace net {

// The key used to group HttpStreams that don't require proxies.
// Currently SocketTag is not supported.
// TODO(crbug.com/346835898): Support SocketTag.
class NET_EXPORT_PRIVATE HttpStreamKey {
 public:
  HttpStreamKey();
  HttpStreamKey(url::SchemeHostPort destination,
                PrivacyMode privacy_mode,
                SocketTag socket_tag,
                NetworkAnonymizationKey network_anonymization_key,
                SecureDnsPolicy secure_dns_policy,
                bool disable_cert_network_fetches);

  ~HttpStreamKey();

  HttpStreamKey(const HttpStreamKey& other);
  HttpStreamKey& operator=(const HttpStreamKey& other);

  bool operator==(const HttpStreamKey& other) const;
  bool operator<(const HttpStreamKey& other) const;

  const url::SchemeHostPort& destination() const { return destination_; }

  PrivacyMode privacy_mode() const { return privacy_mode_; }

  const SocketTag& socket_tag() const { return socket_tag_; }

  const NetworkAnonymizationKey& network_anonymization_key() const {
    return network_anonymization_key_;
  }

  SecureDnsPolicy secure_dns_policy() const { return secure_dns_policy_; }

  bool disable_cert_network_fetches() const {
    return disable_cert_network_fetches_;
  }

  std::string ToString() const;

  base::Value::Dict ToValue() const;

  // Calculate a SpdySessionKey from `this`. Unlike
  // CalculateQuicSessionAliasKey(), this method doesn't take an optional
  // destination because we don't use a different destination for
  // SpdySessionKey.
  // TODO(crbug.com/346835898): We may need to create SpdySessionAliasKey and
  // use a different destination to support H2 alternative endpoints that have
  // different destinations. Returns a key with an empty host when the scheme is
  // not cryptographic.
  SpdySessionKey CalculateSpdySessionKey() const;

  // Calculates a QuicSessionAliasKey from `this`. When
  // `optional_alias_name` is provided, use the destination as the destination
  // of QuicSessionAliasKey. See the comment of QuicSessionAliasKey about the
  // difference between the server id and the destination. Returns a key with
  // empty server_id/destination when the scheme is not cryptographic.
  QuicSessionAliasKey CalculateQuicSessionAliasKey(
      std::optional<url::SchemeHostPort> optional_alias_name =
          std::nullopt) const;

 private:
  url::SchemeHostPort destination_;
  PrivacyMode privacy_mode_ = PrivacyMode::PRIVACY_MODE_DISABLED;
  SocketTag socket_tag_;
  NetworkAnonymizationKey network_anonymization_key_;
  SecureDnsPolicy secure_dns_policy_ = SecureDnsPolicy::kAllow;
  bool disable_cert_network_fetches_ = false;
};

}  // namespace net

#endif  // NET_HTTP_HTTP_STREAM_KEY_H_
