// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_UDP_NET_LOG_PARAMETERS_H_
#define NET_SOCKET_UDP_NET_LOG_PARAMETERS_H_

#include "net/base/network_change_notifier.h"
#include "net/log/net_log_parameters_callback.h"

namespace net {

class IPEndPoint;

// Creates a NetLog callback that returns parameters describing a UDP
// receive/send event.  |bytes| are only logged when byte logging is
// enabled.  |address| may be NULL.  |address| (if given) and |bytes|
// must be valid for the life of the callback.
NetLogParametersCallback CreateNetLogUDPDataTranferCallback(
    int byte_count,
    const char* bytes,
    const IPEndPoint* address);

// Creates a NetLog callback that returns parameters describing a UDP
// connect event.  |address| cannot be NULL, and must remain valid for
// the lifetime of the callback.
NetLogParametersCallback CreateNetLogUDPConnectCallback(
    const IPEndPoint* address,
    NetworkChangeNotifier::NetworkHandle network);

}  // namespace net

#endif  // NET_SOCKET_UDP_NET_LOG_PARAMETERS_H_
