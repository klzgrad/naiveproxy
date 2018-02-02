// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/layered_network_delegate.h"

#include <utility>

namespace net {

LayeredNetworkDelegate::LayeredNetworkDelegate(
    std::unique_ptr<NetworkDelegate> nested_network_delegate)
    : nested_network_delegate_(std::move(nested_network_delegate)) {}

LayeredNetworkDelegate::~LayeredNetworkDelegate() = default;

int LayeredNetworkDelegate::OnBeforeURLRequest(
    URLRequest* request,
    const CompletionCallback& callback,
    GURL* new_url) {
  OnBeforeURLRequestInternal(request, callback, new_url);
  return nested_network_delegate_->NotifyBeforeURLRequest(request, callback,
                                                          new_url);
}

void LayeredNetworkDelegate::OnBeforeURLRequestInternal(
    URLRequest* request,
    const CompletionCallback& callback,
    GURL* new_url) {
}

int LayeredNetworkDelegate::OnBeforeStartTransaction(
    URLRequest* request,
    const CompletionCallback& callback,
    HttpRequestHeaders* headers) {
  OnBeforeStartTransactionInternal(request, callback, headers);
  return nested_network_delegate_->NotifyBeforeStartTransaction(
      request, callback, headers);
}

void LayeredNetworkDelegate::OnBeforeStartTransactionInternal(
    URLRequest* request,
    const CompletionCallback& callback,
    HttpRequestHeaders* headers) {}

void LayeredNetworkDelegate::OnBeforeSendHeaders(
    URLRequest* request,
    const ProxyInfo& proxy_info,
    const ProxyRetryInfoMap& proxy_retry_info,
    HttpRequestHeaders* headers) {
  OnBeforeSendHeadersInternal(request, proxy_info, proxy_retry_info, headers);
  nested_network_delegate_->NotifyBeforeSendHeaders(request, proxy_info,
                                                    proxy_retry_info, headers);
}

void LayeredNetworkDelegate::OnBeforeSendHeadersInternal(
    URLRequest* request,
    const ProxyInfo& proxy_info,
    const ProxyRetryInfoMap& proxy_retry_info,
    HttpRequestHeaders* headers) {}

void LayeredNetworkDelegate::OnStartTransaction(
    URLRequest* request,
    const HttpRequestHeaders& headers) {
  OnStartTransactionInternal(request, headers);
  nested_network_delegate_->NotifyStartTransaction(request, headers);
}

void LayeredNetworkDelegate::OnStartTransactionInternal(
    URLRequest* request,
    const HttpRequestHeaders& headers) {}

int LayeredNetworkDelegate::OnHeadersReceived(
    URLRequest* request,
    const CompletionCallback& callback,
    const HttpResponseHeaders* original_response_headers,
    scoped_refptr<HttpResponseHeaders>* override_response_headers,
    GURL* allowed_unsafe_redirect_url) {
  OnHeadersReceivedInternal(request, callback, original_response_headers,
                            override_response_headers,
                            allowed_unsafe_redirect_url);
  return nested_network_delegate_->NotifyHeadersReceived(
      request, callback, original_response_headers, override_response_headers,
      allowed_unsafe_redirect_url);
}

void LayeredNetworkDelegate::OnHeadersReceivedInternal(
    URLRequest* request,
    const CompletionCallback& callback,
    const HttpResponseHeaders* original_response_headers,
    scoped_refptr<HttpResponseHeaders>* override_response_headers,
    GURL* allowed_unsafe_redirect_url) {
}

void LayeredNetworkDelegate::OnBeforeRedirect(URLRequest* request,
                                              const GURL& new_location) {
  OnBeforeRedirectInternal(request, new_location);
  nested_network_delegate_->NotifyBeforeRedirect(request, new_location);
}

void LayeredNetworkDelegate::OnBeforeRedirectInternal(
    URLRequest* request,
    const GURL& new_location) {
}

void LayeredNetworkDelegate::OnResponseStarted(URLRequest* request,
                                               int net_error) {
  OnResponseStartedInternal(request);
  nested_network_delegate_->NotifyResponseStarted(request, net_error);
}

void LayeredNetworkDelegate::OnResponseStartedInternal(URLRequest* request) {
}

void LayeredNetworkDelegate::OnNetworkBytesReceived(URLRequest* request,
                                                    int64_t bytes_received) {
  OnNetworkBytesReceivedInternal(request, bytes_received);
  nested_network_delegate_->NotifyNetworkBytesReceived(request, bytes_received);
}

void LayeredNetworkDelegate::OnNetworkBytesReceivedInternal(
    URLRequest* request,
    int64_t bytes_received) {}

void LayeredNetworkDelegate::OnNetworkBytesSent(URLRequest* request,
                                                int64_t bytes_sent) {
  OnNetworkBytesSentInternal(request, bytes_sent);
  nested_network_delegate_->NotifyNetworkBytesSent(request, bytes_sent);
}

void LayeredNetworkDelegate::OnNetworkBytesSentInternal(URLRequest* request,
                                                        int64_t bytes_sent) {}

void LayeredNetworkDelegate::OnCompleted(URLRequest* request,
                                         bool started,
                                         int net_error) {
  OnCompletedInternal(request, started);
  nested_network_delegate_->NotifyCompleted(request, started, net_error);
}

void LayeredNetworkDelegate::OnCompletedInternal(URLRequest* request,
                                                 bool started) {}

void LayeredNetworkDelegate::OnURLRequestDestroyed(URLRequest* request) {
  OnURLRequestDestroyedInternal(request);
  nested_network_delegate_->NotifyURLRequestDestroyed(request);
}

void LayeredNetworkDelegate::OnURLRequestDestroyedInternal(
    URLRequest* request) {
}

void LayeredNetworkDelegate::OnPACScriptError(int line_number,
                                              const base::string16& error) {
  OnPACScriptErrorInternal(line_number, error);
  nested_network_delegate_->NotifyPACScriptError(line_number, error);
}

void LayeredNetworkDelegate::OnPACScriptErrorInternal(
    int line_number,
    const base::string16& error) {
}

NetworkDelegate::AuthRequiredResponse LayeredNetworkDelegate::OnAuthRequired(
    URLRequest* request,
    const AuthChallengeInfo& auth_info,
    const AuthCallback& callback,
    AuthCredentials* credentials) {
  OnAuthRequiredInternal(request, auth_info, callback, credentials);
  return nested_network_delegate_->NotifyAuthRequired(request, auth_info,
                                                      callback, credentials);
}

void LayeredNetworkDelegate::OnAuthRequiredInternal(
    URLRequest* request,
    const AuthChallengeInfo& auth_info,
    const AuthCallback& callback,
    AuthCredentials* credentials) {
}

bool LayeredNetworkDelegate::OnCanGetCookies(const URLRequest& request,
                                             const CookieList& cookie_list) {
  OnCanGetCookiesInternal(request, cookie_list);
  return nested_network_delegate_->CanGetCookies(request, cookie_list);
}

void LayeredNetworkDelegate::OnCanGetCookiesInternal(
    const URLRequest& request,
    const CookieList& cookie_list) {
}

bool LayeredNetworkDelegate::OnCanSetCookie(const URLRequest& request,
                                            const net::CanonicalCookie& cookie,
                                            CookieOptions* options) {
  OnCanSetCookieInternal(request, cookie, options);
  return nested_network_delegate_->CanSetCookie(request, cookie, options);
}

void LayeredNetworkDelegate::OnCanSetCookieInternal(
    const URLRequest& request,
    const net::CanonicalCookie& cookie,
    CookieOptions* options) {}

bool LayeredNetworkDelegate::OnCanAccessFile(
    const URLRequest& request,
    const base::FilePath& original_path,
    const base::FilePath& absolute_path) const {
  OnCanAccessFileInternal(request, original_path, absolute_path);
  return nested_network_delegate_->CanAccessFile(request, original_path,
                                                 absolute_path);
}

void LayeredNetworkDelegate::OnCanAccessFileInternal(
    const URLRequest& request,
    const base::FilePath& original_path,
    const base::FilePath& absolute_path) const {}

bool LayeredNetworkDelegate::OnCanEnablePrivacyMode(
    const GURL& url,
    const GURL& site_for_cookies) const {
  OnCanEnablePrivacyModeInternal(url, site_for_cookies);
  return nested_network_delegate_->CanEnablePrivacyMode(url, site_for_cookies);
}

void LayeredNetworkDelegate::OnCanEnablePrivacyModeInternal(
    const GURL& url,
    const GURL& site_for_cookies) const {}

bool LayeredNetworkDelegate::OnAreExperimentalCookieFeaturesEnabled() const {
  OnAreExperimentalCookieFeaturesEnabledInternal();
  return nested_network_delegate_->AreExperimentalCookieFeaturesEnabled();
}

void LayeredNetworkDelegate::OnAreExperimentalCookieFeaturesEnabledInternal()
    const {}

bool LayeredNetworkDelegate::
    OnCancelURLRequestWithPolicyViolatingReferrerHeader(
        const URLRequest& request,
        const GURL& target_url,
        const GURL& referrer_url) const {
  OnCancelURLRequestWithPolicyViolatingReferrerHeaderInternal(
      request, target_url, referrer_url);
  return nested_network_delegate_
      ->CancelURLRequestWithPolicyViolatingReferrerHeader(request, target_url,
                                                          referrer_url);
}

void LayeredNetworkDelegate::
    OnCancelURLRequestWithPolicyViolatingReferrerHeaderInternal(
        const URLRequest& request,
        const GURL& target_url,
        const GURL& referrer_url) const {
}

bool LayeredNetworkDelegate::OnCanQueueReportingReport(
    const url::Origin& origin) const {
  OnCanQueueReportingReportInternal(origin);
  return nested_network_delegate_->CanQueueReportingReport(origin);
}

void LayeredNetworkDelegate::OnCanQueueReportingReportInternal(
    const url::Origin& origin) const {}

bool LayeredNetworkDelegate::OnCanSendReportingReport(
    const url::Origin& origin) const {
  OnCanSendReportingReportInternal(origin);
  return nested_network_delegate_->CanSendReportingReport(origin);
}

void LayeredNetworkDelegate::OnCanSendReportingReportInternal(
    const url::Origin& origin) const {}

bool LayeredNetworkDelegate::OnCanSetReportingClient(
    const url::Origin& origin,
    const GURL& endpoint) const {
  OnCanSetReportingClientInternal(origin, endpoint);
  return nested_network_delegate_->CanSetReportingClient(origin, endpoint);
}

void LayeredNetworkDelegate::OnCanSetReportingClientInternal(
    const url::Origin& origin,
    const GURL& endpoint) const {}

bool LayeredNetworkDelegate::OnCanUseReportingClient(
    const url::Origin& origin,
    const GURL& endpoint) const {
  OnCanUseReportingClientInternal(origin, endpoint);
  return nested_network_delegate_->CanUseReportingClient(origin, endpoint);
}

void LayeredNetworkDelegate::OnCanUseReportingClientInternal(
    const url::Origin& origin,
    const GURL& endpoint) const {}

}  // namespace net
