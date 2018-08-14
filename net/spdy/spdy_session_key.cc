// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_session_key.h"

#include <tuple>

#include "base/logging.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "net/base/host_port_pair.h"

namespace net {

SpdySessionKey::SpdySessionKey() = default;

SpdySessionKey::SpdySessionKey(const HostPortPair& host_port_pair,
                               const ProxyServer& proxy_server,
                               PrivacyMode privacy_mode,
                               const SocketTag& socket_tag)
    : host_port_proxy_pair_(host_port_pair, proxy_server),
      privacy_mode_(privacy_mode),
      socket_tag_(socket_tag) {
  DVLOG(1) << "SpdySessionKey(host=" << host_port_pair.ToString()
      << ", proxy=" << proxy_server.ToURI()
      << ", privacy=" << privacy_mode;
}

SpdySessionKey::SpdySessionKey(const SpdySessionKey& other) = default;

SpdySessionKey::~SpdySessionKey() = default;

bool SpdySessionKey::operator<(const SpdySessionKey& other) const {
  return std::tie(privacy_mode_, host_port_proxy_pair_.first,
                  host_port_proxy_pair_.second, socket_tag_) <
         std::tie(other.privacy_mode_, other.host_port_proxy_pair_.first,
                  other.host_port_proxy_pair_.second, other.socket_tag_);
}

bool SpdySessionKey::operator==(const SpdySessionKey& other) const {
  return privacy_mode_ == other.privacy_mode_ &&
         host_port_proxy_pair_.first.Equals(
             other.host_port_proxy_pair_.first) &&
         host_port_proxy_pair_.second == other.host_port_proxy_pair_.second &&
         socket_tag_ == other.socket_tag_;
}

bool SpdySessionKey::operator!=(const SpdySessionKey& other) const {
  return !(*this == other);
}

size_t SpdySessionKey::EstimateMemoryUsage() const {
  return base::trace_event::EstimateMemoryUsage(host_port_proxy_pair_);
}

}  // namespace net
