// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_quality_estimator_util.h"

#include <memory>

#include "base/bind.h"
#include "base/logging.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/dns/host_resolver.h"
#include "net/dns/host_resolver_source.h"
#include "net/log/net_log_with_source.h"

namespace net {

namespace nqe {

namespace internal {

bool IsPrivateHost(HostResolver* host_resolver,
                   const HostPortPair& host_port_pair) {
  // Try resolving |host_port_pair.host()| synchronously.
  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::LOCAL_ONLY;
  std::unique_ptr<HostResolver::ResolveHostRequest> request =
      host_resolver->CreateRequest(host_port_pair, NetLogWithSource(),
                                   parameters);

  int rv = request->Start(base::BindOnce([](int error) { NOTREACHED(); }));
  DCHECK_NE(rv, ERR_IO_PENDING);

  if (rv == OK && request->GetAddressResults() &&
      !request->GetAddressResults().value().empty()) {
    // Checking only the first address should be sufficient.
    IPEndPoint ip_endpoint = request->GetAddressResults().value().front();
    IPAddress ip_address = ip_endpoint.address();
    if (!ip_address.IsPubliclyRoutable())
      return true;
  }

  return false;
}

}  // namespace internal

}  // namespace nqe

}  // namespace net
