// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_session_key.h"

namespace net {

QuicSessionKey::QuicSessionKey(const HostPortPair& host_port_pair,
                               PrivacyMode privacy_mode,
                               const SocketTag& socket_tag)
    : QuicSessionKey(QuicServerId(host_port_pair, privacy_mode), socket_tag) {}

QuicSessionKey::QuicSessionKey(const std::string& host,
                               uint16_t port,
                               PrivacyMode privacy_mode,
                               const SocketTag& socket_tag)
    : QuicSessionKey(QuicServerId(host, port, privacy_mode), socket_tag) {}

QuicSessionKey::QuicSessionKey(const QuicServerId& server_id,
                               const SocketTag& socket_tag)
    : server_id_(server_id), socket_tag_(socket_tag) {}

bool QuicSessionKey::operator<(const QuicSessionKey& other) const {
  return std::tie(server_id_, socket_tag_) <
         std::tie(other.server_id_, other.socket_tag_);
}
bool QuicSessionKey::operator==(const QuicSessionKey& other) const {
  return server_id_ == other.server_id_ && socket_tag_ == other.socket_tag_;
}

size_t QuicSessionKey::EstimateMemoryUsage() const {
  return server_id_.EstimateMemoryUsage();
}

}  // namespace net