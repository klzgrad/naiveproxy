// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_proxy_client_socket_pool.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "base/compiler_specific.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/optional.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_delegate.h"
#include "net/http/http_network_session.h"
#include "net/http/http_proxy_client_socket_wrapper.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"
#include "net/nqe/network_quality_provider.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/ssl_client_socket_pool.h"
#include "net/socket/transport_client_socket_pool.h"
#include "net/spdy/chromium/spdy_proxy_client_socket.h"
#include "net/spdy/chromium/spdy_session.h"
#include "net/spdy/chromium/spdy_session_pool.h"
#include "net/spdy/chromium/spdy_stream.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "url/gurl.h"

namespace net {

namespace {

// HttpProxyConnectJobs will time out after this many seconds.  Note this is on
// top of the timeout for the transport socket.
// TODO(kundaji): Proxy connect timeout should be independent of platform and be
// based on proxy. Bug http://crbug.com/407446.
#if defined(OS_ANDROID) || defined(OS_IOS)
static const int kHttpProxyConnectJobTimeoutInSeconds = 10;
#else
static const int kHttpProxyConnectJobTimeoutInSeconds = 30;
#endif

static const char kNetAdaptiveProxyConnectionTimeout[] =
    "NetAdaptiveProxyConnectionTimeout";

bool IsInNetAdaptiveProxyConnectionTimeoutFieldTrial() {
  // Field trial is enabled if the group name starts with "Enabled".
  return base::FieldTrialList::FindFullName(kNetAdaptiveProxyConnectionTimeout)
             .find("Enabled") == 0;
}

// Return the value of the parameter |param_name| for the field trial
// |kNetAdaptiveProxyConnectionTimeout|. If the value of the parameter is
// unavailable, then |default_value| is available.
int32_t GetInt32Param(const std::string& param_name, int32_t default_value) {
  int32_t param;
  if (!base::StringToInt(base::GetFieldTrialParamValue(
                             kNetAdaptiveProxyConnectionTimeout, param_name),
                         &param)) {
    return default_value;
  }
  return param;
}

}  // namespace

HttpProxySocketParams::HttpProxySocketParams(
    const scoped_refptr<TransportSocketParams>& transport_params,
    const scoped_refptr<SSLSocketParams>& ssl_params,
    const std::string& user_agent,
    const HostPortPair& endpoint,
    HttpAuthCache* http_auth_cache,
    HttpAuthHandlerFactory* http_auth_handler_factory,
    SpdySessionPool* spdy_session_pool,
    bool tunnel,
    ProxyDelegate* proxy_delegate)
    : transport_params_(transport_params),
      ssl_params_(ssl_params),
      spdy_session_pool_(spdy_session_pool),
      user_agent_(user_agent),
      endpoint_(endpoint),
      http_auth_cache_(tunnel ? http_auth_cache : NULL),
      http_auth_handler_factory_(tunnel ? http_auth_handler_factory : NULL),
      tunnel_(tunnel),
      proxy_delegate_(proxy_delegate) {
  DCHECK((transport_params.get() == NULL && ssl_params.get() != NULL) ||
         (transport_params.get() != NULL && ssl_params.get() == NULL));
}

const HostResolver::RequestInfo& HttpProxySocketParams::destination() const {
  if (transport_params_.get() == NULL) {
    return ssl_params_->GetDirectConnectionParams()->destination();
  } else {
    return transport_params_->destination();
  }
}

HttpProxySocketParams::~HttpProxySocketParams() {}

HttpProxyConnectJob::HttpProxyConnectJob(
    const std::string& group_name,
    RequestPriority priority,
    ClientSocketPool::RespectLimits respect_limits,
    const scoped_refptr<HttpProxySocketParams>& params,
    const base::TimeDelta& timeout_duration,
    TransportClientSocketPool* transport_pool,
    SSLClientSocketPool* ssl_pool,
    Delegate* delegate,
    NetLog* net_log)
    : ConnectJob(
          group_name,
          base::TimeDelta() /* The socket takes care of timeouts */,
          priority,
          respect_limits,
          delegate,
          NetLogWithSource::Make(net_log,
                                 NetLogSourceType::HTTP_PROXY_CONNECT_JOB)),
      client_socket_(new HttpProxyClientSocketWrapper(
          group_name,
          priority,
          respect_limits,
          timeout_duration,
          base::TimeDelta::FromSeconds(kHttpProxyConnectJobTimeoutInSeconds),
          transport_pool,
          ssl_pool,
          params->transport_params(),
          params->ssl_params(),
          params->user_agent(),
          params->endpoint(),
          params->http_auth_cache(),
          params->http_auth_handler_factory(),
          params->spdy_session_pool(),
          params->tunnel(),
          params->proxy_delegate(),
          this->net_log())) {}

HttpProxyConnectJob::~HttpProxyConnectJob() {}

LoadState HttpProxyConnectJob::GetLoadState() const {
  return client_socket_->GetConnectLoadState();
}

void HttpProxyConnectJob::GetAdditionalErrorState(ClientSocketHandle * handle) {
  if (error_response_info_) {
    handle->set_ssl_error_response_info(*error_response_info_);
    handle->set_is_ssl_error(true);
  }
}

int HttpProxyConnectJob::ConnectInternal() {
  int result = client_socket_->Connect(base::Bind(
      &HttpProxyConnectJob::OnConnectComplete, base::Unretained(this)));
  return HandleConnectResult(result);
}

void HttpProxyConnectJob::OnConnectComplete(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  result = HandleConnectResult(result);
  NotifyDelegateOfCompletion(result);
  // |this| will have been deleted at this point.
}

int HttpProxyConnectJob::HandleConnectResult(int result) {
  if (result == ERR_SSL_CLIENT_AUTH_CERT_NEEDED)
    error_response_info_ = client_socket_->GetAdditionalErrorState();

  if (result == OK || result == ERR_PROXY_AUTH_REQUESTED ||
      result == ERR_HTTPS_PROXY_TUNNEL_RESPONSE) {
    SetSocket(std::move(client_socket_));
  }
  return result;
}

HttpProxyClientSocketPool::HttpProxyConnectJobFactory::
    HttpProxyConnectJobFactory(TransportClientSocketPool* transport_pool,
                               SSLClientSocketPool* ssl_pool,
                               NetworkQualityProvider* network_quality_provider,
                               NetLog* net_log)
    : transport_pool_(transport_pool),
      ssl_pool_(ssl_pool),
      network_quality_provider_(network_quality_provider),
      transport_rtt_multiplier_(GetInt32Param("transport_rtt_multiplier", 5)),
      min_proxy_connection_timeout_(base::TimeDelta::FromSeconds(
          GetInt32Param("min_proxy_connection_timeout_seconds", 8))),
      max_proxy_connection_timeout_(base::TimeDelta::FromSeconds(
          GetInt32Param("max_proxy_connection_timeout_seconds", 60))),
      net_log_(net_log) {
  DCHECK_LT(0, transport_rtt_multiplier_);
  DCHECK_LE(base::TimeDelta(), min_proxy_connection_timeout_);
  DCHECK_LE(base::TimeDelta(), max_proxy_connection_timeout_);
  DCHECK_LE(min_proxy_connection_timeout_, max_proxy_connection_timeout_);
}

std::unique_ptr<ConnectJob>
HttpProxyClientSocketPool::HttpProxyConnectJobFactory::NewConnectJob(
    const std::string& group_name,
    const PoolBase::Request& request,
    ConnectJob::Delegate* delegate) const {
  return std::unique_ptr<ConnectJob>(new HttpProxyConnectJob(
      group_name, request.priority(), request.respect_limits(),
      request.params(), ConnectionTimeout(), transport_pool_, ssl_pool_,
      delegate, net_log_));
}

base::TimeDelta
HttpProxyClientSocketPool::HttpProxyConnectJobFactory::ConnectionTimeout()
    const {
  if (IsInNetAdaptiveProxyConnectionTimeoutFieldTrial() &&
      network_quality_provider_) {
    base::Optional<base::TimeDelta> transport_rtt_estimate =
        network_quality_provider_->GetTransportRTT();
    if (transport_rtt_estimate) {
      base::TimeDelta timeout = base::TimeDelta::FromMilliseconds(
          transport_rtt_multiplier_ *
          transport_rtt_estimate.value().InMilliseconds());
      // Ensure that connection timeout is between
      // |min_proxy_connection_timeout_| and |max_proxy_connection_timeout_|.
      if (timeout < min_proxy_connection_timeout_)
        return min_proxy_connection_timeout_;
      if (timeout > max_proxy_connection_timeout_)
        return max_proxy_connection_timeout_;
      return timeout;
    }
  }

  // Return the default proxy connection timeout.
  base::TimeDelta max_pool_timeout = base::TimeDelta();
#if (!defined(OS_ANDROID) && !defined(OS_IOS))
  if (transport_pool_)
    max_pool_timeout = transport_pool_->ConnectionTimeout();
  if (ssl_pool_) {
    max_pool_timeout =
        std::max(max_pool_timeout, ssl_pool_->ConnectionTimeout());
  }
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

  return max_pool_timeout +
         base::TimeDelta::FromSeconds(kHttpProxyConnectJobTimeoutInSeconds);
}

HttpProxyClientSocketPool::HttpProxyClientSocketPool(
    int max_sockets,
    int max_sockets_per_group,
    TransportClientSocketPool* transport_pool,
    SSLClientSocketPool* ssl_pool,
    NetworkQualityProvider* network_quality_provider,
    NetLog* net_log)
    : transport_pool_(transport_pool),
      ssl_pool_(ssl_pool),
      base_(this,
            max_sockets,
            max_sockets_per_group,
            ClientSocketPool::unused_idle_socket_timeout(),
            ClientSocketPool::used_idle_socket_timeout(),
            new HttpProxyConnectJobFactory(transport_pool,
                                           ssl_pool,
                                           network_quality_provider,
                                           net_log)) {
  // We should always have a |transport_pool_| except in unit tests.
  if (transport_pool_)
    base_.AddLowerLayeredPool(transport_pool_);
  if (ssl_pool_)
    base_.AddLowerLayeredPool(ssl_pool_);
}

HttpProxyClientSocketPool::~HttpProxyClientSocketPool() {
}

int HttpProxyClientSocketPool::RequestSocket(const std::string& group_name,
                                             const void* socket_params,
                                             RequestPriority priority,
                                             RespectLimits respect_limits,
                                             ClientSocketHandle* handle,
                                             const CompletionCallback& callback,
                                             const NetLogWithSource& net_log) {
  const scoped_refptr<HttpProxySocketParams>* casted_socket_params =
      static_cast<const scoped_refptr<HttpProxySocketParams>*>(socket_params);

  return base_.RequestSocket(group_name, *casted_socket_params, priority,
                             respect_limits, handle, callback, net_log);
}

void HttpProxyClientSocketPool::RequestSockets(
    const std::string& group_name,
    const void* params,
    int num_sockets,
    const NetLogWithSource& net_log,
    HttpRequestInfo::RequestMotivation motivation) {
  const scoped_refptr<HttpProxySocketParams>* casted_params =
      static_cast<const scoped_refptr<HttpProxySocketParams>*>(params);

  base_.RequestSockets(group_name, *casted_params, num_sockets, net_log,
                       motivation);
}

void HttpProxyClientSocketPool::CancelRequest(
    const std::string& group_name,
    ClientSocketHandle* handle) {
  base_.CancelRequest(group_name, handle);
}

void HttpProxyClientSocketPool::SetPriority(const std::string& group_name,
                                            ClientSocketHandle* handle,
                                            RequestPriority priority) {
  base_.SetPriority(group_name, handle, priority);
}

void HttpProxyClientSocketPool::ReleaseSocket(
    const std::string& group_name,
    std::unique_ptr<StreamSocket> socket,
    int id) {
  base_.ReleaseSocket(group_name, std::move(socket), id);
}

void HttpProxyClientSocketPool::FlushWithError(int error) {
  base_.FlushWithError(error);
}

void HttpProxyClientSocketPool::CloseIdleSockets() {
  base_.CloseIdleSockets();
}

void HttpProxyClientSocketPool::CloseIdleSocketsInGroup(
    const std::string& group_name) {
  base_.CloseIdleSocketsInGroup(group_name);
}

int HttpProxyClientSocketPool::IdleSocketCount() const {
  return base_.idle_socket_count();
}

int HttpProxyClientSocketPool::IdleSocketCountInGroup(
    const std::string& group_name) const {
  return base_.IdleSocketCountInGroup(group_name);
}

LoadState HttpProxyClientSocketPool::GetLoadState(
    const std::string& group_name, const ClientSocketHandle* handle) const {
  return base_.GetLoadState(group_name, handle);
}

std::unique_ptr<base::DictionaryValue>
HttpProxyClientSocketPool::GetInfoAsValue(const std::string& name,
                                          const std::string& type,
                                          bool include_nested_pools) const {
  std::unique_ptr<base::DictionaryValue> dict(base_.GetInfoAsValue(name, type));
  if (include_nested_pools) {
    auto list = std::make_unique<base::ListValue>();
    if (transport_pool_) {
      list->Append(transport_pool_->GetInfoAsValue("transport_socket_pool",
                                                   "transport_socket_pool",
                                                   true));
    }
    if (ssl_pool_) {
      list->Append(ssl_pool_->GetInfoAsValue("ssl_socket_pool",
                                             "ssl_socket_pool",
                                             true));
    }
    dict->Set("nested_pools", std::move(list));
  }
  return dict;
}

base::TimeDelta HttpProxyClientSocketPool::ConnectionTimeout() const {
  return base_.ConnectionTimeout();
}

bool HttpProxyClientSocketPool::IsStalled() const {
  return base_.IsStalled();
}

void HttpProxyClientSocketPool::AddHigherLayeredPool(
    HigherLayeredPool* higher_pool) {
  base_.AddHigherLayeredPool(higher_pool);
}

void HttpProxyClientSocketPool::RemoveHigherLayeredPool(
    HigherLayeredPool* higher_pool) {
  base_.RemoveHigherLayeredPool(higher_pool);
}

bool HttpProxyClientSocketPool::CloseOneIdleConnection() {
  if (base_.CloseOneIdleSocket())
    return true;
  return base_.CloseOneIdleConnectionInHigherLayeredPool();
}

}  // namespace net
