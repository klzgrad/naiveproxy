// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_factory_test_util.h"

#include <utility>

#include "net/proxy/proxy_info.h"

using ::testing::_;

namespace net {
MockHttpStreamRequestDelegate::MockHttpStreamRequestDelegate() {}

MockHttpStreamRequestDelegate::~MockHttpStreamRequestDelegate() {}

MockHttpStreamFactoryImplJob::MockHttpStreamFactoryImplJob(
    HttpStreamFactoryImpl::Job::Delegate* delegate,
    HttpStreamFactoryImpl::JobType job_type,
    HttpNetworkSession* session,
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    ProxyInfo proxy_info,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HostPortPair destination,
    GURL origin_url,
    NextProto alternative_protocol,
    QuicTransportVersion quic_version,
    const ProxyServer& alternative_proxy_server,
    bool enable_ip_based_pooling,
    NetLog* net_log)
    : HttpStreamFactoryImpl::Job(delegate,
                                 job_type,
                                 session,
                                 request_info,
                                 priority,
                                 proxy_info,
                                 server_ssl_config,
                                 proxy_ssl_config,
                                 destination,
                                 origin_url,
                                 alternative_protocol,
                                 quic_version,
                                 alternative_proxy_server,
                                 enable_ip_based_pooling,
                                 net_log) {
  DCHECK(!is_waiting());
}

MockHttpStreamFactoryImplJob::~MockHttpStreamFactoryImplJob() {}

TestJobFactory::TestJobFactory()
    : main_job_(nullptr),
      alternative_job_(nullptr),
      override_main_job_url_(false) {}

TestJobFactory::~TestJobFactory() {}

std::unique_ptr<HttpStreamFactoryImpl::Job> TestJobFactory::CreateMainJob(
    HttpStreamFactoryImpl::Job::Delegate* delegate,
    HttpStreamFactoryImpl::JobType job_type,
    HttpNetworkSession* session,
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const ProxyInfo& proxy_info,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HostPortPair destination,
    GURL origin_url,
    bool enable_ip_based_pooling,
    NetLog* net_log) {
  if (override_main_job_url_)
    origin_url = main_job_alternative_url_;

  auto main_job = std::make_unique<MockHttpStreamFactoryImplJob>(
      delegate, job_type, session, request_info, priority, proxy_info,
      SSLConfig(), SSLConfig(), destination, origin_url, kProtoUnknown,
      QUIC_VERSION_UNSUPPORTED, ProxyServer(), enable_ip_based_pooling,
      net_log);

  // Keep raw pointer to Job but pass ownership.
  main_job_ = main_job.get();

  return std::move(main_job);
}

std::unique_ptr<HttpStreamFactoryImpl::Job> TestJobFactory::CreateAltSvcJob(
    HttpStreamFactoryImpl::Job::Delegate* delegate,
    HttpStreamFactoryImpl::JobType job_type,
    HttpNetworkSession* session,
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const ProxyInfo& proxy_info,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HostPortPair destination,
    GURL origin_url,
    NextProto alternative_protocol,
    QuicTransportVersion quic_version,
    bool enable_ip_based_pooling,
    NetLog* net_log) {
  auto alternative_job = std::make_unique<MockHttpStreamFactoryImplJob>(
      delegate, job_type, session, request_info, priority, proxy_info,
      SSLConfig(), SSLConfig(), destination, origin_url, alternative_protocol,
      quic_version, ProxyServer(), enable_ip_based_pooling, net_log);

  // Keep raw pointer to Job but pass ownership.
  alternative_job_ = alternative_job.get();

  return std::move(alternative_job);
}

std::unique_ptr<HttpStreamFactoryImpl::Job> TestJobFactory::CreateAltProxyJob(
    HttpStreamFactoryImpl::Job::Delegate* delegate,
    HttpStreamFactoryImpl::JobType job_type,
    HttpNetworkSession* session,
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const ProxyInfo& proxy_info,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HostPortPair destination,
    GURL origin_url,
    const ProxyServer& alternative_proxy_server,
    bool enable_ip_based_pooling,
    NetLog* net_log) {
  auto alternative_job = std::make_unique<MockHttpStreamFactoryImplJob>(
      delegate, job_type, session, request_info, priority, proxy_info,
      SSLConfig(), SSLConfig(), destination, origin_url, kProtoUnknown,
      QUIC_VERSION_UNSUPPORTED, alternative_proxy_server,
      enable_ip_based_pooling, net_log);

  // Keep raw pointer to Job but pass ownership.
  alternative_job_ = alternative_job.get();

  return std::move(alternative_job);
}

}  // namespace net
