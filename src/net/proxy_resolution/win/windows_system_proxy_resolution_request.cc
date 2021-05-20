// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/win/windows_system_proxy_resolution_request.h"

#include <utility>

#include "net/base/net_errors.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/proxy_resolution/proxy_list.h"
#include "net/proxy_resolution/win/windows_system_proxy_resolution_service.h"
#include "net/proxy_resolution/win/windows_system_proxy_resolver.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

namespace {

constexpr net::NetworkTrafficAnnotationTag kWindowsResolverTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("proxy_config_windows_resolver", R"(
      semantics {
        sender: "Proxy Config for Windows System Resolver"
        description:
          "Establishing a connection through a proxy server using system proxy "
          "settings and Windows system proxy resolution code."
        trigger:
          "Whenever a network request is made when the system proxy settings "
          "are used, the Windows system proxy resolver is enabled, and the "
          "result indicates usage of a proxy server."
        data:
          "Proxy configuration."
        destination: OTHER
        destination_other:
          "The proxy server specified in the configuration."
      }
      policy {
        cookies_allowed: NO
        setting:
          "User cannot override system proxy settings, but can change them "
          "through 'Advanced/System/Open proxy settings'."
        policy_exception_justification:
          "Using either of 'ProxyMode', 'ProxyServer', or 'ProxyPacUrl' "
          "policies can set Chrome to use a specific proxy settings and avoid "
          "system proxy."
      })");

}  // namespace

WindowsSystemProxyResolutionRequest::WindowsSystemProxyResolutionRequest(
    WindowsSystemProxyResolutionService* service,
    const GURL& url,
    const std::string& method,
    ProxyInfo* results,
    CompletionOnceCallback user_callback,
    const NetLogWithSource& net_log,
    scoped_refptr<WindowsSystemProxyResolver> windows_system_proxy_resolver)
    : windows_system_proxy_resolver_(windows_system_proxy_resolver),
      service_(service),
      user_callback_(std::move(user_callback)),
      results_(results),
      url_(url),
      method_(method),
      net_log_(net_log),
      creation_time_(base::TimeTicks::Now()) {
  DCHECK(!user_callback_.is_null());
  DCHECK(windows_system_proxy_resolver_);
}

WindowsSystemProxyResolutionRequest::~WindowsSystemProxyResolutionRequest() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (service_) {
    service_->RemovePendingRequest(this);
    net_log_.AddEvent(NetLogEventType::CANCELLED);

    if (IsStarted())
      CancelResolveJob();

    net_log_.EndEvent(NetLogEventType::PROXY_RESOLUTION_SERVICE);
  }
}

LoadState WindowsSystemProxyResolutionRequest::GetLoadState() const {
  // TODO(https://crbug.com/1032820): Consider adding a LoadState for "We're
  // waiting on system APIs to do their thing".
  return LOAD_STATE_RESOLVING_PROXY_FOR_URL;
}

int WindowsSystemProxyResolutionRequest::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!was_completed());
  DCHECK(!IsStarted());

  // Kicks off an asynchronous call that'll eventually call back into
  // AsynchronousProxyResolutionComplete() with a result.
  if (!windows_system_proxy_resolver_->GetProxyForUrl(this, url_.spec()))
    return ERR_FAILED;

  // Asynchronous proxy resolution has begun.
  return ERR_IO_PENDING;
}

void WindowsSystemProxyResolutionRequest::CancelResolveJob() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(IsStarted());
  // The request may already be running in the resolver.
  // TODO(https://crbug.com/1032820): Cancel callback instead of just ignoring
  // it.
  windows_system_proxy_resolver_->RemovePendingCallbackTarget(this);
  DCHECK(!IsStarted());
}

bool WindowsSystemProxyResolutionRequest::IsStarted() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return windows_system_proxy_resolver_->HasPendingCallbackTarget(this);
}

int WindowsSystemProxyResolutionRequest::UpdateResultsOnProxyResolutionComplete(
    const ProxyList& proxy_list,
    int net_error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!was_completed());

  results_->UseProxyList(proxy_list);

  // Make sure IsStarted() returns false while DidFinishResolvingProxy() runs.
  windows_system_proxy_resolver_->RemovePendingCallbackTarget(this);

  // Note that DidFinishResolvingProxy might modify |results_|.
  const int updated_result = service_->DidFinishResolvingProxy(
      url_, method_, results_, net_error, net_log_);

  // Make a note in the results which configuration was in use at the
  // time of the resolve.
  results_->set_proxy_resolve_start_time(creation_time_);
  results_->set_proxy_resolve_end_time(base::TimeTicks::Now());
  results_->set_traffic_annotation(
      MutableNetworkTrafficAnnotationTag(kWindowsResolverTrafficAnnotation));

  return updated_result;
}

int WindowsSystemProxyResolutionRequest::SynchronousProxyResolutionComplete(
    int net_error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ProxyList proxy_list;
  const int updated_result =
      UpdateResultsOnProxyResolutionComplete(proxy_list, net_error);
  service_ = nullptr;
  return updated_result;
}

void WindowsSystemProxyResolutionRequest::AsynchronousProxyResolutionComplete(
    const ProxyList& proxy_list,
    int net_error,
    int windows_error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(https://crbug.com/1032820): Log Windows error |windows_error|.

  net_error = UpdateResultsOnProxyResolutionComplete(proxy_list, net_error);

  CompletionOnceCallback callback = std::move(user_callback_);

  service_->RemovePendingRequest(this);
  service_ = nullptr;
  user_callback_.Reset();
  std::move(callback).Run(net_error);
}

}  // namespace net
