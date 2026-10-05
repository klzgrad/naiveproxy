// Copyright 2026 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_PROTOCOL_H_
#define NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_PROTOCOL_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "net/base/auth.h"
#include "net/base/host_port_pair.h"
#include "net/http/http_request_headers.h"

namespace net {

HttpRequestHeaders BuildWebSocketTunnelHeaders(
    const AuthCredentials& credentials);

std::optional<std::vector<uint8_t>> EncodeWebSocketTarget(
    const HostPortPair& target);

}  // namespace net

#endif  // NET_TOOLS_NAIVE_WEBSOCKET_TUNNEL_PROTOCOL_H_
