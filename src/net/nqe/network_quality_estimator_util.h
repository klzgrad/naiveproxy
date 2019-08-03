// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NQE_NETWORK_QUALITY_ESTIMATOR_UTIL_H_
#define NET_NQE_NETWORK_QUALITY_ESTIMATOR_UTIL_H_

#include <stdint.h>

#include "net/base/net_export.h"

namespace net {

class HostPortPair;
class HostResolver;

namespace nqe {

namespace internal {

// A unified compact representation of an IPv6 or an IPv4 address.
typedef uint64_t IPHash;

// Returns true if the host contained in |host_port_pair| is a host in a
// private Internet as defined by RFC 1918 or if the requests to
// |host_port_pair| are not expected to generate useful network quality
// information. This includes localhost, hosts on private subnets, and
// hosts on subnets that are reserved for specific usage, and are unlikely
// to be used by public web servers.
// To make this determination, IsPrivateHost() makes the best
// effort estimate including trying to resolve the host in the
// |host_port_pair|. The method is synchronous.
// |host_resolver| must not be null.
NET_EXPORT_PRIVATE bool IsPrivateHost(HostResolver* host_resolver,
                                      const HostPortPair& host_port_pair);

}  // namespace internal

}  // namespace nqe

}  // namespace net

#endif  // NET_NQE_NETWORK_QUALITY_ESTIMATOR_UTIL_H_
