// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/mapped_host_resolver.h"

#include <utility>

#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"

namespace net {

MappedHostResolver::MappedHostResolver(std::unique_ptr<HostResolver> impl)
    : impl_(std::move(impl)) {}

MappedHostResolver::~MappedHostResolver() {
}

int MappedHostResolver::Resolve(const RequestInfo& original_info,
                                RequestPriority priority,
                                AddressList* addresses,
                                const CompletionCallback& callback,
                                std::unique_ptr<Request>* request,
                                const NetLogWithSource& net_log) {
  RequestInfo info = original_info;
  int rv = ApplyRules(&info);
  if (rv != OK)
    return rv;

  return impl_->Resolve(info, priority, addresses, callback, request, net_log);
}

int MappedHostResolver::ResolveFromCache(const RequestInfo& original_info,
                                         AddressList* addresses,
                                         const NetLogWithSource& net_log) {
  RequestInfo info = original_info;
  int rv = ApplyRules(&info);
  if (rv != OK)
    return rv;

  return impl_->ResolveFromCache(info, addresses, net_log);
}

void MappedHostResolver::SetDnsClientEnabled(bool enabled) {
  impl_->SetDnsClientEnabled(enabled);
}

HostCache* MappedHostResolver::GetHostCache() {
  return impl_->GetHostCache();
}

std::unique_ptr<base::Value> MappedHostResolver::GetDnsConfigAsValue() const {
  return impl_->GetDnsConfigAsValue();
}

void MappedHostResolver::SetNoIPv6OnWifi(bool no_ipv6_on_wifi) {
  impl_->SetNoIPv6OnWifi(no_ipv6_on_wifi);
}

bool MappedHostResolver::GetNoIPv6OnWifi() {
  return impl_->GetNoIPv6OnWifi();
}

int MappedHostResolver::ApplyRules(RequestInfo* info) const {
  HostPortPair host_port(info->host_port_pair());
  if (rules_.RewriteHost(&host_port)) {
    if (host_port.host() == "~NOTFOUND")
      return ERR_NAME_NOT_RESOLVED;
    info->set_host_port_pair(host_port);
  }
  return OK;
}

}  // namespace net
