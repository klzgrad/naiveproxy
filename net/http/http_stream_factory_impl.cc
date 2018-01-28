// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_factory_impl.h"

#include <tuple>
#include <utility>

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "base/trace_event/process_memory_dump.h"
#include "net/http/http_network_session.h"
#include "net/http/http_server_properties.h"
#include "net/http/http_stream_factory_impl_job.h"
#include "net/http/http_stream_factory_impl_job_controller.h"
#include "net/http/http_stream_factory_impl_request.h"
#include "net/http/transport_security_state.h"
#include "net/proxy/proxy_info.h"
#include "net/quic/core/quic_server_id.h"
#include "net/spdy/chromium/bidirectional_stream_spdy_impl.h"
#include "net/spdy/chromium/spdy_http_stream.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"
#include "url/url_constants.h"

namespace net {

HttpStreamFactoryImpl::HttpStreamFactoryImpl(HttpNetworkSession* session,
                                             bool for_websockets)
    : session_(session),
      job_factory_(new JobFactory()),
      for_websockets_(for_websockets),
      last_logged_job_controller_count_(0) {}

HttpStreamFactoryImpl::~HttpStreamFactoryImpl() {
  UMA_HISTOGRAM_COUNTS_1M("Net.JobControllerSet.CountOfJobControllerAtShutDown",
                          job_controller_set_.size());
}

std::unique_ptr<HttpStreamRequest> HttpStreamFactoryImpl::RequestStream(
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpStreamRequest::Delegate* delegate,
    bool enable_ip_based_pooling,
    bool enable_alternative_services,
    const NetLogWithSource& net_log) {
  DCHECK(!for_websockets_);
  return RequestStreamInternal(
      request_info, priority, server_ssl_config, proxy_ssl_config, delegate,
      nullptr, HttpStreamRequest::HTTP_STREAM, enable_ip_based_pooling,
      enable_alternative_services, net_log);
}

std::unique_ptr<HttpStreamRequest>
HttpStreamFactoryImpl::RequestWebSocketHandshakeStream(
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpStreamRequest::Delegate* delegate,
    WebSocketHandshakeStreamBase::CreateHelper* create_helper,
    bool enable_ip_based_pooling,
    bool enable_alternative_services,
    const NetLogWithSource& net_log) {
  DCHECK(for_websockets_);
  DCHECK(create_helper);
  return RequestStreamInternal(
      request_info, priority, server_ssl_config, proxy_ssl_config, delegate,
      create_helper, HttpStreamRequest::HTTP_STREAM, enable_ip_based_pooling,
      enable_alternative_services, net_log);
}

std::unique_ptr<HttpStreamRequest>
HttpStreamFactoryImpl::RequestBidirectionalStreamImpl(
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpStreamRequest::Delegate* delegate,
    bool enable_ip_based_pooling,
    bool enable_alternative_services,
    const NetLogWithSource& net_log) {
  DCHECK(!for_websockets_);
  DCHECK(request_info.url.SchemeIs(url::kHttpsScheme));

  return RequestStreamInternal(
      request_info, priority, server_ssl_config, proxy_ssl_config, delegate,
      nullptr, HttpStreamRequest::BIDIRECTIONAL_STREAM, enable_ip_based_pooling,
      enable_alternative_services, net_log);
}

std::unique_ptr<HttpStreamRequest> HttpStreamFactoryImpl::RequestStreamInternal(
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpStreamRequest::Delegate* delegate,
    WebSocketHandshakeStreamBase::CreateHelper*
        websocket_handshake_stream_create_helper,
    HttpStreamRequest::StreamType stream_type,
    bool enable_ip_based_pooling,
    bool enable_alternative_services,
    const NetLogWithSource& net_log) {
  AddJobControllerCountToHistograms();

  auto job_controller = std::make_unique<JobController>(
      this, delegate, session_, job_factory_.get(), request_info,
      /* is_preconnect = */ false, enable_ip_based_pooling,
      enable_alternative_services, server_ssl_config, proxy_ssl_config);
  JobController* job_controller_raw_ptr = job_controller.get();
  job_controller_set_.insert(std::move(job_controller));
  return job_controller_raw_ptr->Start(delegate,
                                       websocket_handshake_stream_create_helper,
                                       net_log, stream_type, priority);
}

void HttpStreamFactoryImpl::PreconnectStreams(
    int num_streams,
    const HttpRequestInfo& request_info) {
  DCHECK(request_info.url.is_valid());

  AddJobControllerCountToHistograms();

  SSLConfig server_ssl_config;
  SSLConfig proxy_ssl_config;
  session_->GetSSLConfig(request_info, &server_ssl_config, &proxy_ssl_config);
  // All preconnects should perform EV certificate verification.
  server_ssl_config.verify_ev_cert = true;
  proxy_ssl_config.verify_ev_cert = true;

  DCHECK(!for_websockets_);

  auto job_controller = std::make_unique<JobController>(
      this, nullptr, session_, job_factory_.get(), request_info,
      /* is_preconnect = */ true,
      /* enable_ip_based_pooling = */ true,
      /* enable_alternative_services = */ true, server_ssl_config,
      proxy_ssl_config);
  JobController* job_controller_raw_ptr = job_controller.get();
  job_controller_set_.insert(std::move(job_controller));
  job_controller_raw_ptr->Preconnect(num_streams);
}

const HostMappingRules* HttpStreamFactoryImpl::GetHostMappingRules() const {
  return &session_->params().host_mapping_rules;
}

void HttpStreamFactoryImpl::OnJobControllerComplete(JobController* controller) {
  for (auto it = job_controller_set_.begin(); it != job_controller_set_.end();
       ++it) {
    if (it->get() == controller) {
      job_controller_set_.erase(it);
      return;
    }
  }
  NOTREACHED();
}

HttpStreamFactoryImpl::PreconnectingProxyServer::PreconnectingProxyServer(
    ProxyServer proxy_server,
    PrivacyMode privacy_mode)
    : proxy_server(proxy_server), privacy_mode(privacy_mode) {}

bool HttpStreamFactoryImpl::PreconnectingProxyServer::operator<(
    const PreconnectingProxyServer& other) const {
  return std::tie(proxy_server, privacy_mode) <
         std::tie(other.proxy_server, other.privacy_mode);
}

bool HttpStreamFactoryImpl::PreconnectingProxyServer::operator==(
    const PreconnectingProxyServer& other) const {
  return proxy_server == other.proxy_server &&
         privacy_mode == other.privacy_mode;
}

bool HttpStreamFactoryImpl::OnInitConnection(const JobController& controller,
                                             const ProxyInfo& proxy_info,
                                             PrivacyMode privacy_mode) {
  if (!controller.is_preconnect()) {
    // Connection initialization can be skipped only for the preconnect jobs.
    return false;
  }

  if (!ProxyServerSupportsPriorities(proxy_info))
    return false;

  PreconnectingProxyServer preconnecting_proxy_server(proxy_info.proxy_server(),
                                                      privacy_mode);

  if (base::ContainsKey(preconnecting_proxy_servers_,
                        preconnecting_proxy_server)) {
    UMA_HISTOGRAM_EXACT_LINEAR("Net.PreconnectSkippedToProxyServers", 1, 2);
    // Skip preconnect to the proxy server since we are already preconnecting
    // (probably via some other job). See crbug.com/682041 for details.
    return true;
  }

  // Add the proxy server to the set of preconnecting proxy servers.
  // The maximum size of |preconnecting_proxy_servers_|.
  static const size_t kMaxPreconnectingServerSize = 3;
  if (preconnecting_proxy_servers_.size() >= kMaxPreconnectingServerSize) {
    // Erase the first entry. A better approach (at the cost of higher memory
    // overhead) may be to erase the least recently used entry.
    preconnecting_proxy_servers_.erase(preconnecting_proxy_servers_.begin());
  }

  preconnecting_proxy_servers_.insert(preconnecting_proxy_server);
  DCHECK_GE(kMaxPreconnectingServerSize, preconnecting_proxy_servers_.size());
  // The first preconnect should be allowed.
  return false;
}

void HttpStreamFactoryImpl::OnStreamReady(const ProxyInfo& proxy_info,
                                          PrivacyMode privacy_mode) {
  if (proxy_info.is_empty())
    return;
  preconnecting_proxy_servers_.erase(
      PreconnectingProxyServer(proxy_info.proxy_server(), privacy_mode));
}

bool HttpStreamFactoryImpl::ProxyServerSupportsPriorities(
    const ProxyInfo& proxy_info) const {
  if (proxy_info.is_empty() || !proxy_info.proxy_server().is_valid())
    return false;

  if (!proxy_info.proxy_server().is_https())
    return false;

  HostPortPair host_port_pair = proxy_info.proxy_server().host_port_pair();
  DCHECK(!host_port_pair.IsEmpty());

  url::SchemeHostPort scheme_host_port("https", host_port_pair.host(),
                                       host_port_pair.port());

  return session_->http_server_properties()->SupportsRequestPriority(
      scheme_host_port);
}

void HttpStreamFactoryImpl::AddJobControllerCountToHistograms() {
  // Only log the count of JobControllers when the count is hitting one of the
  // boundaries for the first time which is a multiple of 100: 100, 200, 300,
  // etc.
  if (job_controller_set_.size() % 100 != 0 ||
      job_controller_set_.size() <= last_logged_job_controller_count_) {
    return;
  }
  last_logged_job_controller_count_ = job_controller_set_.size();

  UMA_HISTOGRAM_COUNTS_1M("Net.JobControllerSet.CountOfJobController",
                          job_controller_set_.size());

  size_t num_controllers_with_request = 0;
  size_t num_controllers_for_preconnect = 0;
  for (const auto& job_controller : job_controller_set_) {
    DCHECK(job_controller->HasPendingAltJob() ||
           job_controller->HasPendingMainJob());
    // Additionally logs the states of the jobs if there are at least 500
    // controllers, which suggests that there might be a leak.
    if (job_controller_set_.size() >= 500)
      job_controller->LogHistograms();
    // For a preconnect controller, it should have exactly the main job.
    if (job_controller->is_preconnect()) {
      num_controllers_for_preconnect++;
      continue;
    }
    // For non-preconnects.
    if (job_controller->HasPendingRequest())
      num_controllers_with_request++;
  }
  UMA_HISTOGRAM_COUNTS_1M(
      "Net.JobControllerSet.CountOfJobController.Preconnect",
      num_controllers_for_preconnect);
  UMA_HISTOGRAM_COUNTS_1M(
      "Net.JobControllerSet.CountOfJobController.NonPreconnect.PendingRequest",
      num_controllers_with_request);
  UMA_HISTOGRAM_COUNTS_1M(
      "Net.JobControllerSet.CountOfJobController.NonPreconnect.RequestGone",
      job_controller_set_.size() - num_controllers_for_preconnect -
          num_controllers_with_request);
}

void HttpStreamFactoryImpl::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_absolute_name) const {
  if (job_controller_set_.empty())
    return;
  std::string name =
      base::StringPrintf("%s/stream_factory", parent_absolute_name.c_str());
  base::trace_event::MemoryAllocatorDump* factory_dump =
      pmd->CreateAllocatorDump(name);
  size_t alt_job_count = 0;
  size_t main_job_count = 0;
  size_t num_controllers_for_preconnect = 0;
  for (const auto& it : job_controller_set_) {
    // For a preconnect controller, it should have exactly the main job.
    if (it->is_preconnect()) {
      num_controllers_for_preconnect++;
      continue;
    }
    // For non-preconnects.
    if (it->HasPendingAltJob())
      alt_job_count++;
    if (it->HasPendingMainJob())
      main_job_count++;
  }
  factory_dump->AddScalar(
      base::trace_event::MemoryAllocatorDump::kNameSize,
      base::trace_event::MemoryAllocatorDump::kUnitsBytes,
      base::trace_event::EstimateMemoryUsage(job_controller_set_));
  factory_dump->AddScalar(
      base::trace_event::MemoryAllocatorDump::kNameObjectCount,
      base::trace_event::MemoryAllocatorDump::kUnitsObjects,
      job_controller_set_.size());
  // The number of non-preconnect controllers with a pending alt job.
  factory_dump->AddScalar("alt_job_count",
                          base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                          alt_job_count);
  // The number of non-preconnect controllers with a pending main job.
  factory_dump->AddScalar("main_job_count",
                          base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                          main_job_count);
  // The number of preconnect controllers.
  factory_dump->AddScalar("preconnect_count",
                          base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                          num_controllers_for_preconnect);
}

}  // namespace net
