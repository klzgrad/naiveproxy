// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_RESOLUTION_WIN_WINDOWS_SYSTEM_PROXY_RESOLUTION_SERVICE_H_
#define NET_PROXY_RESOLUTION_WIN_WINDOWS_SYSTEM_PROXY_RESOLUTION_SERVICE_H_

#include "net/proxy_resolution/proxy_resolution_service.h"

#include <string>

#include "net/base/net_export.h"

namespace net {

// This class decides which proxy server(s) to use for a particular URL request.
// It does NOT support passing in fetched proxy configurations. Instead, it
// relies entirely on WinHttp APIs to determine the proxy that should be used
// for each network request.
class NET_EXPORT WindowsSystemProxyResolutionService
    : public ProxyResolutionService {
 public:
  WindowsSystemProxyResolutionService();

  WindowsSystemProxyResolutionService(
      const WindowsSystemProxyResolutionService&) = delete;
  WindowsSystemProxyResolutionService& operator=(
      const WindowsSystemProxyResolutionService&) = delete;

  ~WindowsSystemProxyResolutionService() override;

  // ProxyResolutionService implementation
  int ResolveProxy(const GURL& url,
                   const std::string& method,
                   const NetworkIsolationKey& network_isolation_key,
                   ProxyInfo* results,
                   CompletionOnceCallback callback,
                   std::unique_ptr<ProxyResolutionRequest>* request,
                   const NetLogWithSource& net_log) override;
  void ReportSuccess(const ProxyInfo& proxy_info) override;
  void SetProxyDelegate(ProxyDelegate* delegate) override;
  void OnShutdown() override;
  bool MarkProxiesAsBadUntil(
      const ProxyInfo& results,
      base::TimeDelta retry_delay,
      const std::vector<ProxyServer>& additional_bad_proxies,
      const NetLogWithSource& net_log) override;
  void ClearBadProxiesCache() override;
  const ProxyRetryInfoMap& proxy_retry_info() const override;
  std::unique_ptr<base::DictionaryValue> GetProxyNetLogValues(
      int info_sources) override;
  bool CastToConfiguredProxyResolutionService(
      ConfiguredProxyResolutionService** configured_proxy_resolution_service)
      override WARN_UNUSED_RESULT;

 private:
  // Map of the known bad proxies and the information about the retry time.
  ProxyRetryInfoMap proxy_retry_info_;
};

}  // namespace net

#endif  // NET_PROXY_RESOLUTION_WIN_WINDOWS_SYSTEM_PROXY_RESOLUTION_SERVICE_H_
