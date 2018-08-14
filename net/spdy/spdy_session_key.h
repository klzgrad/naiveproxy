// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_SPDY_SESSION_KEY_H_
#define NET_SPDY_SPDY_SESSION_KEY_H_

#include "net/base/net_export.h"
#include "net/base/privacy_mode.h"
#include "net/base/proxy_server.h"
#include "net/socket/socket_tag.h"

namespace net {

// SpdySessionKey is used as unique index for SpdySessionPool.
class NET_EXPORT_PRIVATE SpdySessionKey {
 public:
  SpdySessionKey();
  SpdySessionKey(const HostPortPair& host_port_pair,
                 const ProxyServer& proxy_server,
                 PrivacyMode privacy_mode,
                 const SocketTag& socket_tag);

  SpdySessionKey(const SpdySessionKey& other);

  ~SpdySessionKey();

  // Comparator function so this can be placed in a std::map.
  bool operator<(const SpdySessionKey& other) const;

  // Equality tests of contents.
  bool operator==(const SpdySessionKey& other) const;
  bool operator!=(const SpdySessionKey& other) const;

  const HostPortProxyPair& host_port_proxy_pair() const {
    return host_port_proxy_pair_;
  }

  const HostPortPair& host_port_pair() const {
    return host_port_proxy_pair_.first;
  }

  const ProxyServer& proxy_server() const {
    return host_port_proxy_pair_.second;
  }

  PrivacyMode privacy_mode() const {
    return privacy_mode_;
  }

  const SocketTag& socket_tag() const { return socket_tag_; }

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 private:
  HostPortProxyPair host_port_proxy_pair_;
  // If enabled, then session cannot be tracked by the server.
  PrivacyMode privacy_mode_ = PRIVACY_MODE_DISABLED;
  SocketTag socket_tag_;
};

}  // namespace net

#endif  // NET_SPDY_SPDY_SESSION_KEY_H_
