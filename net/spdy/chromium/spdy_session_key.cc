// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/spdy_session_key.h"

#include <tuple>

#include "base/logging.h"
#include "net/base/host_port_pair.h"
#include "net/spdy/platform/api/spdy_estimate_memory_usage.h"

namespace net {

SpdySessionKey::SpdySessionKey() : privacy_mode_(PRIVACY_MODE_DISABLED) {
}

SpdySessionKey::SpdySessionKey(const HostPortPair& host_port_pair,
                               const ProxyServer& proxy_server,
                               PrivacyMode privacy_mode)
    : host_port_proxy_pair_(host_port_pair, proxy_server),
      privacy_mode_(privacy_mode) {
  DVLOG(1) << "SpdySessionKey(host=" << host_port_pair.ToString()
      << ", proxy=" << proxy_server.ToURI()
      << ", privacy=" << privacy_mode;
}

SpdySessionKey::SpdySessionKey(const HostPortProxyPair& host_port_proxy_pair,
                               PrivacyMode privacy_mode)
    : host_port_proxy_pair_(host_port_proxy_pair),
      privacy_mode_(privacy_mode) {
  DVLOG(1) << "SpdySessionKey(hppp=" << host_port_proxy_pair.first.ToString()
      << "," << host_port_proxy_pair.second.ToURI()
      << ", privacy=" << privacy_mode;
}

SpdySessionKey::SpdySessionKey(const SpdySessionKey& other) = default;

SpdySessionKey::~SpdySessionKey() {}

bool SpdySessionKey::operator<(const SpdySessionKey& other) const {
  return std::tie(privacy_mode_, host_port_proxy_pair_.first,
                  host_port_proxy_pair_.second) <
         std::tie(other.privacy_mode_, other.host_port_proxy_pair_.first,
                  other.host_port_proxy_pair_.second);
}

bool SpdySessionKey::Equals(const SpdySessionKey& other) const {
  return privacy_mode_ == other.privacy_mode_ &&
      host_port_proxy_pair_.first.Equals(other.host_port_proxy_pair_.first) &&
      host_port_proxy_pair_.second == other.host_port_proxy_pair_.second;
}

size_t SpdySessionKey::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(host_port_proxy_pair_);
}

}  // namespace net
