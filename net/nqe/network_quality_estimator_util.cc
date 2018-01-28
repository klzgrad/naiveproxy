// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_quality_estimator_util.h"

#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/dns/host_resolver.h"
#include "net/log/net_log_with_source.h"

namespace net {

namespace nqe {

namespace internal {

bool IsPrivateHost(HostResolver* host_resolver,
                   const HostPortPair& host_port_pair) {
  // Try resolving |host_port_pair.host()| synchronously.
  HostResolver::RequestInfo resolve_info(host_port_pair);
  resolve_info.set_allow_cached_response(true);
  AddressList addresses;
  // Resolve synchronously using the resolver's cache.
  int rv = host_resolver->ResolveFromCache(resolve_info, &addresses,
                                           NetLogWithSource());

  DCHECK_NE(rv, ERR_IO_PENDING);
  if (rv == OK && !addresses.empty()) {
    // Checking only the first address should be sufficient.
    IPEndPoint ip_end_point = addresses.front();
    net::IPAddress ip_address = ip_end_point.address();
    if (ip_address.IsReserved())
      return true;
  }

  return false;
}

}  // namespace internal

}  // namespace nqe

}  // namespace net
