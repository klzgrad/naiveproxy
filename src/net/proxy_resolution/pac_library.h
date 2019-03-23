// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_RESOLUTION_PAC_LIBRARY_H_
#define NET_PROXY_RESOLUTION_PAC_LIBRARY_H_

#include "net/base/ip_address.h"
#include "net/base/net_export.h"

// TODO(eroman): Move other PAC library support functions into here.

namespace net {

class ClientSocketFactory;
class AddressList;

// Implementations for myIpAddress() and myIpAddressEx() function calls
// available in the PAC environment. These are expected to be called on a worker
// thread as they may block.
//
// Do not use these outside of PAC as they are broken APIs. See comments in the
// implementation file for details.
NET_EXPORT_PRIVATE IPAddressList PacMyIpAddress();
NET_EXPORT_PRIVATE IPAddressList PacMyIpAddressEx();

// Test exposed variants that allows mocking the UDP and DNS dependencies.
NET_EXPORT_PRIVATE IPAddressList
PacMyIpAddressForTest(ClientSocketFactory* socket_factory,
                      const AddressList& dns_result);
NET_EXPORT_PRIVATE IPAddressList
PacMyIpAddressExForTest(ClientSocketFactory* socket_factory,
                        const AddressList& dns_result);

}  // namespace net

#endif  // NET_PROXY_RESOLUTION_PAC_LIBRARY_H_
