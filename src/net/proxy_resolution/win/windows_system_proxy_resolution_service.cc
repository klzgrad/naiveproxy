// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/win/windows_system_proxy_resolution_service.h"

#include "base/values.h"
#include "net/base/net_errors.h"

namespace net {

WindowsSystemProxyResolutionService::WindowsSystemProxyResolutionService() =
    default;
WindowsSystemProxyResolutionService::~WindowsSystemProxyResolutionService() =
    default;

int WindowsSystemProxyResolutionService::ResolveProxy(
    const GURL& url,
    const std::string& method,
    const NetworkIsolationKey& network_isolation_key,
    ProxyInfo* results,
    CompletionOnceCallback callback,
    std::unique_ptr<ProxyResolutionRequest>* request,
    const NetLogWithSource& net_log) {
  return ERR_NOT_IMPLEMENTED;
}

void WindowsSystemProxyResolutionService::ReportSuccess(
    const ProxyInfo& proxy_info) {}

void WindowsSystemProxyResolutionService::SetProxyDelegate(
    ProxyDelegate* delegate) {}

void WindowsSystemProxyResolutionService::OnShutdown() {}

bool WindowsSystemProxyResolutionService::MarkProxiesAsBadUntil(
    const ProxyInfo& results,
    base::TimeDelta retry_delay,
    const std::vector<ProxyServer>& additional_bad_proxies,
    const NetLogWithSource& net_log) {
  return false;
}

void WindowsSystemProxyResolutionService::ClearBadProxiesCache() {}

const ProxyRetryInfoMap& WindowsSystemProxyResolutionService::proxy_retry_info()
    const {
  return proxy_retry_info_;
}

std::unique_ptr<base::DictionaryValue>
WindowsSystemProxyResolutionService::GetProxyNetLogValues(int info_sources) {
  std::unique_ptr<base::DictionaryValue> net_info_dict(
      new base::DictionaryValue());
  return net_info_dict;
}

bool WindowsSystemProxyResolutionService::
    CastToConfiguredProxyResolutionService(
        ConfiguredProxyResolutionService**
            configured_proxy_resolution_service) {
  *configured_proxy_resolution_service = nullptr;
  return false;
}

}  // namespace net
