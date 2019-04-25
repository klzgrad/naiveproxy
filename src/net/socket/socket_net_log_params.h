// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SOCKET_NET_LOG_PARAMS_H_
#define NET_SOCKET_SOCKET_NET_LOG_PARAMS_H_

#include "net/base/sys_addrinfo.h"
#include "net/log/net_log_parameters_callback.h"

namespace net {

class HostPortPair;
class IPEndPoint;

// Creates a NetLog callback for socket error events.
NetLogParametersCallback CreateNetLogSocketErrorCallback(int net_error,
                                                         int os_error);

// Creates a NetLog callback for a HostPortPair.
// |host_and_port| must remain valid for the lifetime of the returned callback.
NetLogParametersCallback CreateNetLogHostPortPairCallback(
    const HostPortPair* host_and_port);

// Creates a NetLog callback for an IPEndPoint.
// |address| must remain valid for the lifetime of the returned callback.
NetLogParametersCallback CreateNetLogIPEndPointCallback(
    const IPEndPoint* address);

// Creates a NetLog callback for the source sockaddr on connect events.
// |net_address| must remain valid for the lifetime of the returned callback.
NetLogParametersCallback CreateNetLogSourceAddressCallback(
    const struct sockaddr* net_address,
    socklen_t address_len);

}  // namespace net

#endif  // NET_SOCKET_SOCKET_NET_LOG_PARAMS_H_
