// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_DELEGATE_IMPL_H_
#define NET_BASE_NETWORK_DELEGATE_IMPL_H_

#include <stdint.h>

#include "base/strings/string16.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/base/network_delegate.h"
#include "net/cookies/canonical_cookie.h"
#include "net/proxy/proxy_retry_info.h"

class GURL;

namespace base {
class FilePath;
}

namespace url {
class Origin;
}

namespace net {

class CookieOptions;
class HttpRequestHeaders;
class HttpResponseHeaders;
class ProxyInfo;
class URLRequest;

class NET_EXPORT NetworkDelegateImpl : public NetworkDelegate {
 public:
  ~NetworkDelegateImpl() override {}

 private:
  int OnBeforeURLRequest(URLRequest* request,
                         const CompletionCallback& callback,
                         GURL* new_url) override;

  int OnBeforeStartTransaction(URLRequest* request,
                               const CompletionCallback& callback,
                               HttpRequestHeaders* headers) override;

  void OnBeforeSendHeaders(URLRequest* request,
                           const ProxyInfo& proxy_info,
                           const ProxyRetryInfoMap& proxy_retry_info,
                           HttpRequestHeaders* headers) override;

  void OnStartTransaction(URLRequest* request,
                          const HttpRequestHeaders& headers) override;

  int OnHeadersReceived(
      URLRequest* request,
      const CompletionCallback& callback,
      const HttpResponseHeaders* original_response_headers,
      scoped_refptr<HttpResponseHeaders>* override_response_headers,
      GURL* allowed_unsafe_redirect_url) override;

  void OnBeforeRedirect(URLRequest* request, const GURL& new_location) override;

  void OnResponseStarted(URLRequest* request, int net_error) override;

  void OnNetworkBytesReceived(URLRequest* request,
                              int64_t bytes_received) override;

  void OnNetworkBytesSent(URLRequest* request, int64_t bytes_sent) override;

  void OnCompleted(URLRequest* request, bool started, int net_error) override;
  void OnCompleted(URLRequest* request, bool started) override;

  void OnURLRequestDestroyed(URLRequest* request) override;

  void OnPACScriptError(int line_number, const base::string16& error) override;

  AuthRequiredResponse OnAuthRequired(URLRequest* request,
                                      const AuthChallengeInfo& auth_info,
                                      const AuthCallback& callback,
                                      AuthCredentials* credentials) override;

  bool OnCanGetCookies(const URLRequest& request,
                       const CookieList& cookie_list) override;

  bool OnCanSetCookie(const URLRequest& request,
                      const net::CanonicalCookie& cookie,
                      CookieOptions* options) override;

  bool OnCanAccessFile(const URLRequest& request,
                       const base::FilePath& original_path,
                       const base::FilePath& absolute_path) const override;

  bool OnCanEnablePrivacyMode(const GURL& url,
                              const GURL& site_for_cookies) const override;

  bool OnAreExperimentalCookieFeaturesEnabled() const override;

  bool OnCancelURLRequestWithPolicyViolatingReferrerHeader(
      const URLRequest& request,
      const GURL& target_url,
      const GURL& referrer_url) const override;

  bool OnCanQueueReportingReport(const url::Origin& origin) const override;

  bool OnCanSendReportingReport(const url::Origin& origin) const override;

  bool OnCanSetReportingClient(const url::Origin& origin,
                               const GURL& endpoint) const override;

  bool OnCanUseReportingClient(const url::Origin& origin,
                               const GURL& endpoint) const override;
};

}  // namespace net

#endif  // NET_BASE_NETWORK_DELEGATE_IMPL_H_
