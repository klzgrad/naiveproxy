// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_SERVER_ID_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_SERVER_ID_H_

#include <cstdint>

#include "net/base/host_port_pair.h"
#include "net/base/privacy_mode.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace net {

// The id used to identify sessions. Includes the hostname, port, scheme and
// privacy_mode.
class QUIC_EXPORT_PRIVATE QuicServerId {
 public:
  QuicServerId();
  QuicServerId(const HostPortPair& host_port_pair, PrivacyMode privacy_mode);
  QuicServerId(const QuicString& host, uint16_t port);
  QuicServerId(const QuicString& host, uint16_t port, PrivacyMode privacy_mode);
  ~QuicServerId();

  // Needed to be an element of std::set.
  bool operator<(const QuicServerId& other) const;
  bool operator==(const QuicServerId& other) const;

  // ToString() will convert the QuicServerId to "scheme:hostname:port" or
  // "scheme:hostname:port/private". "scheme" will be "https".
  QuicString ToString() const;

  // Used in Chromium, but not internally.
  const HostPortPair& host_port_pair() const { return host_port_pair_; }

  const QuicString& host() const { return host_port_pair_.host(); }

  uint16_t port() const { return host_port_pair_.port(); }

  PrivacyMode privacy_mode() const { return privacy_mode_; }

  size_t EstimateMemoryUsage() const;

 private:
  HostPortPair host_port_pair_;
  PrivacyMode privacy_mode_;
};

}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_SERVER_ID_H_
