// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_QUIC_SESSION_KEY_H_
#define NET_QUIC_CHROMIUM_QUIC_SESSION_KEY_H_

#include "net/socket/socket_tag.h"
#include "net/third_party/quic/core/quic_server_id.h"

namespace net {

// The key used to identify sessions. Includes the QuicServerId and socket tag.
class QUIC_EXPORT_PRIVATE QuicSessionKey {
 public:
  QuicSessionKey() = default;
  QuicSessionKey(const HostPortPair& host_port_pair,
                 PrivacyMode privacy_mode,
                 const SocketTag& socket_tag);
  QuicSessionKey(const std::string& host,
                 uint16_t port,
                 PrivacyMode privacy_mode,
                 const SocketTag& socket_tag);
  QuicSessionKey(const QuicServerId& server_id, const SocketTag& socket_tag);
  ~QuicSessionKey() = default;

  // Needed to be an element of std::set.
  bool operator<(const QuicSessionKey& other) const;
  bool operator==(const QuicSessionKey& other) const;

  const std::string& host() const { return server_id_.host(); }

  PrivacyMode privacy_mode() const { return server_id_.privacy_mode(); }

  const QuicServerId& server_id() const { return server_id_; }

  SocketTag socket_tag() const { return socket_tag_; }

  size_t EstimateMemoryUsage() const;

 private:
  QuicServerId server_id_;
  SocketTag socket_tag_;
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_SERVER_ID_H_
