// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_PROXY_CONNECT_JOB_H_
#define NET_HTTP_HTTP_PROXY_CONNECT_JOB_H_

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/http/http_auth.h"
#include "net/http/http_response_info.h"
#include "net/socket/connect_job.h"
#include "net/socket/ssl_client_socket.h"
#include "net/spdy/spdy_session.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class HttpAuthCache;
class HttpAuthHandlerFactory;
class HttpProxyClientSocketWrapper;
class NetworkQualityEstimator;
class SpdySessionPool;
class SSLSocketParams;
class TransportSocketParams;
class QuicStreamFactory;

// HttpProxySocketParams only needs the socket params for one of the proxy
// types.  The other param must be NULL.  When using an HTTP proxy,
// |transport_params| must be set.  When using an HTTPS proxy or QUIC proxy,
// |ssl_params| must be set. Also, if using a QUIC proxy, |quic_version| must
// not be quic::QUIC_VERSION_UNSUPPORTED.
class NET_EXPORT_PRIVATE HttpProxySocketParams
    : public base::RefCounted<HttpProxySocketParams> {
 public:
  HttpProxySocketParams(
      const scoped_refptr<TransportSocketParams>& transport_params,
      const scoped_refptr<SSLSocketParams>& ssl_params,
      quic::QuicTransportVersion quic_version,
      const std::string& user_agent,
      const HostPortPair& endpoint,
      HttpAuthCache* http_auth_cache,
      HttpAuthHandlerFactory* http_auth_handler_factory,
      SpdySessionPool* spdy_session_pool,
      QuicStreamFactory* quic_stream_factory,
      bool is_trusted_proxy,
      bool tunnel,
      const NetworkTrafficAnnotationTag traffic_annotation);

  const scoped_refptr<TransportSocketParams>& transport_params() const {
    return transport_params_;
  }
  const scoped_refptr<SSLSocketParams>& ssl_params() const {
    return ssl_params_;
  }
  quic::QuicTransportVersion quic_version() const { return quic_version_; }
  const std::string& user_agent() const { return user_agent_; }
  const HostPortPair& endpoint() const { return endpoint_; }
  HttpAuthCache* http_auth_cache() const { return http_auth_cache_; }
  HttpAuthHandlerFactory* http_auth_handler_factory() const {
    return http_auth_handler_factory_;
  }
  SpdySessionPool* spdy_session_pool() { return spdy_session_pool_; }
  QuicStreamFactory* quic_stream_factory() const {
    return quic_stream_factory_;
  }
  bool is_trusted_proxy() const { return is_trusted_proxy_; }
  bool tunnel() const { return tunnel_; }
  const NetworkTrafficAnnotationTag traffic_annotation() const {
    return traffic_annotation_;
  }

 private:
  friend class base::RefCounted<HttpProxySocketParams>;
  ~HttpProxySocketParams();

  const scoped_refptr<TransportSocketParams> transport_params_;
  const scoped_refptr<SSLSocketParams> ssl_params_;
  quic::QuicTransportVersion quic_version_;
  SpdySessionPool* spdy_session_pool_;
  QuicStreamFactory* quic_stream_factory_;
  const std::string user_agent_;
  const HostPortPair endpoint_;
  HttpAuthCache* const http_auth_cache_;
  HttpAuthHandlerFactory* const http_auth_handler_factory_;
  const bool is_trusted_proxy_;
  const bool tunnel_;
  const NetworkTrafficAnnotationTag traffic_annotation_;

  DISALLOW_COPY_AND_ASSIGN(HttpProxySocketParams);
};

// HttpProxyConnectJob optionally establishes a tunnel through the proxy
// server after connecting the underlying transport socket.
class NET_EXPORT_PRIVATE HttpProxyConnectJob : public ConnectJob {
 public:
  HttpProxyConnectJob(RequestPriority priority,
                      const CommonConnectJobParams& common_connect_job_params,
                      const scoped_refptr<HttpProxySocketParams>& params,
                      Delegate* delegate,
                      const NetLogWithSource* net_log);
  ~HttpProxyConnectJob() override;

  // ConnectJob methods.
  LoadState GetLoadState() const override;
  bool HasEstablishedConnection() const override;

  void OnNeedsProxyAuth(const HttpResponseInfo& response,
                        HttpAuthController* auth_controller,
                        base::OnceClosure restart_with_auth_callback);

  void GetAdditionalErrorState(ClientSocketHandle* handle) override;

  // Returns the connection timeout that will be used by a HttpProxyConnectJob
  // created with the specified parameters, given current network conditions.
  static base::TimeDelta ConnectionTimeout(
      const HttpProxySocketParams& params,
      const NetworkQualityEstimator* network_quality_estimator);

  // Returns the timeout for establishing a tunnel after a connection has been
  // established.
  static base::TimeDelta TunnelTimeoutForTesting();

  // Updates the field trial parameters used in calculating timeouts.
  static void UpdateFieldTrialParametersForTesting();

 private:
  // Begins the tcp connection and the optional Http proxy tunnel.  If the
  // request is not immediately serviceable (likely), the request will return
  // ERR_IO_PENDING. An OK return from this function or the callback means
  // that the connection is established; ERR_PROXY_AUTH_REQUESTED means
  // that the tunnel needs authentication credentials, the socket will be
  // returned in this case, and must be released back to the pool; or
  // a standard net error code will be returned.
  int ConnectInternal() override;

  void ChangePriorityInternal(RequestPriority priority) override;

  void OnConnectComplete(int result);

  int HandleConnectResult(int result);

  std::unique_ptr<HttpProxyClientSocketWrapper> client_socket_;

  scoped_refptr<HttpProxySocketParams> params_;

  std::unique_ptr<HttpResponseInfo> error_response_info_;

  DISALLOW_COPY_AND_ASSIGN(HttpProxyConnectJob);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_PROXY_CONNECT_JOB_H_
