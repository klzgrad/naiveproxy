// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_factory.h"

#include <tuple>
#include <utility>

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "base/trace_event/process_memory_dump.h"
#include "net/base/host_mapping_rules.h"
#include "net/base/host_port_pair.h"
#include "net/base/parse_number.h"
#include "net/base/port_util.h"
#include "net/http/http_network_session.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_server_properties.h"
#include "net/http/http_stream_factory_job.h"
#include "net/http/http_stream_factory_job_controller.h"
#include "net/http/transport_security_state.h"
#include "net/quic/quic_http_utils.h"
#include "net/spdy/bidirectional_stream_spdy_impl.h"
#include "net/spdy/spdy_http_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/spdy/core/spdy_alt_svc_wire_format.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"
#include "url/url_constants.h"

namespace net {

HttpStreamFactory::HttpStreamFactory(HttpNetworkSession* session)
    : session_(session), job_factory_(std::make_unique<JobFactory>()) {}

HttpStreamFactory::~HttpStreamFactory() {}

void HttpStreamFactory::ProcessAlternativeServices(
    HttpNetworkSession* session,
    const HttpResponseHeaders* headers,
    const url::SchemeHostPort& http_server) {
  if (!headers->HasHeader(kAlternativeServiceHeader))
    return;

  std::string alternative_service_str;
  headers->GetNormalizedHeader(kAlternativeServiceHeader,
                               &alternative_service_str);
  spdy::SpdyAltSvcWireFormat::AlternativeServiceVector
      alternative_service_vector;
  if (!spdy::SpdyAltSvcWireFormat::ParseHeaderFieldValue(
          alternative_service_str, &alternative_service_vector)) {
    return;
  }

  // Convert spdy::SpdyAltSvcWireFormat::AlternativeService entries
  // to net::AlternativeServiceInfo.
  AlternativeServiceInfoVector alternative_service_info_vector;
  for (const spdy::SpdyAltSvcWireFormat::AlternativeService&
           alternative_service_entry : alternative_service_vector) {
    NextProto protocol =
        NextProtoFromString(alternative_service_entry.protocol_id);
    if (!IsAlternateProtocolValid(protocol) ||
        !session->IsProtocolEnabled(protocol) ||
        !IsPortValid(alternative_service_entry.port)) {
      continue;
    }
    // Check if QUIC version is supported. Filter supported QUIC versions.
    quic::ParsedQuicVersionVector advertised_versions;
    if (protocol == kProtoQUIC && !alternative_service_entry.version.empty()) {
      advertised_versions = FilterSupportedAltSvcVersions(
          alternative_service_entry,
          session->params().quic_params.supported_versions,
          session->params().quic_params.support_ietf_format_quic_altsvc);
      if (advertised_versions.empty())
        continue;
    }
    AlternativeService alternative_service(protocol,
                                           alternative_service_entry.host,
                                           alternative_service_entry.port);
    base::Time expiration =
        base::Time::Now() +
        base::TimeDelta::FromSeconds(alternative_service_entry.max_age);
    AlternativeServiceInfo alternative_service_info;
    if (protocol == kProtoQUIC) {
      alternative_service_info =
          AlternativeServiceInfo::CreateQuicAlternativeServiceInfo(
              alternative_service, expiration, advertised_versions);
    } else {
      alternative_service_info =
          AlternativeServiceInfo::CreateHttp2AlternativeServiceInfo(
              alternative_service, expiration);
    }
    alternative_service_info_vector.push_back(alternative_service_info);
  }

  session->http_server_properties()->SetAlternativeServices(
      RewriteHost(http_server), alternative_service_info_vector);
}

url::SchemeHostPort HttpStreamFactory::RewriteHost(
    const url::SchemeHostPort& server) {
  HostPortPair host_port_pair(server.host(), server.port());
  const HostMappingRules* mapping_rules = GetHostMappingRules();
  if (mapping_rules)
    mapping_rules->RewriteHost(&host_port_pair);
  return url::SchemeHostPort(server.scheme(), host_port_pair.host(),
                             host_port_pair.port());
}

std::unique_ptr<HttpStreamRequest> HttpStreamFactory::RequestStream(
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpStreamRequest::Delegate* delegate,
    bool enable_ip_based_pooling,
    bool enable_alternative_services,
    const NetLogWithSource& net_log) {
  return RequestStreamInternal(
      request_info, priority, server_ssl_config, proxy_ssl_config, delegate,
      nullptr, HttpStreamRequest::HTTP_STREAM, false /* is_websocket */,
      enable_ip_based_pooling, enable_alternative_services, net_log);
}

std::unique_ptr<HttpStreamRequest>
HttpStreamFactory::RequestWebSocketHandshakeStream(
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpStreamRequest::Delegate* delegate,
    WebSocketHandshakeStreamBase::CreateHelper* create_helper,
    bool enable_ip_based_pooling,
    bool enable_alternative_services,
    const NetLogWithSource& net_log) {
  DCHECK(create_helper);
  return RequestStreamInternal(
      request_info, priority, server_ssl_config, proxy_ssl_config, delegate,
      create_helper, HttpStreamRequest::HTTP_STREAM, true /* is_websocket */,
      enable_ip_based_pooling, enable_alternative_services, net_log);
}

std::unique_ptr<HttpStreamRequest>
HttpStreamFactory::RequestBidirectionalStreamImpl(
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpStreamRequest::Delegate* delegate,
    bool enable_ip_based_pooling,
    bool enable_alternative_services,
    const NetLogWithSource& net_log) {
  DCHECK(request_info.url.SchemeIs(url::kHttpsScheme));

  return RequestStreamInternal(
      request_info, priority, server_ssl_config, proxy_ssl_config, delegate,
      nullptr, HttpStreamRequest::BIDIRECTIONAL_STREAM,
      false /* is_websocket */, enable_ip_based_pooling,
      enable_alternative_services, net_log);
}

std::unique_ptr<HttpStreamRequest> HttpStreamFactory::RequestStreamInternal(
    const HttpRequestInfo& request_info,
    RequestPriority priority,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpStreamRequest::Delegate* delegate,
    WebSocketHandshakeStreamBase::CreateHelper*
        websocket_handshake_stream_create_helper,
    HttpStreamRequest::StreamType stream_type,
    bool is_websocket,
    bool enable_ip_based_pooling,
    bool enable_alternative_services,
    const NetLogWithSource& net_log) {
  auto job_controller = std::make_unique<JobController>(
      this, delegate, session_, job_factory_.get(), request_info,
      /* is_preconnect = */ false, is_websocket, enable_ip_based_pooling,
      enable_alternative_services, server_ssl_config, proxy_ssl_config);
  JobController* job_controller_raw_ptr = job_controller.get();
  job_controller_set_.insert(std::move(job_controller));
  return job_controller_raw_ptr->Start(delegate,
                                       websocket_handshake_stream_create_helper,
                                       net_log, stream_type, priority);
}

void HttpStreamFactory::PreconnectStreams(int num_streams,
                                          const HttpRequestInfo& request_info) {
  DCHECK(request_info.url.is_valid());

  SSLConfig server_ssl_config;
  SSLConfig proxy_ssl_config;
  session_->GetSSLConfig(request_info, &server_ssl_config, &proxy_ssl_config);

  auto job_controller = std::make_unique<JobController>(
      this, nullptr, session_, job_factory_.get(), request_info,
      /* is_preconnect = */ true,
      /* is_websocket = */ false,
      /* enable_ip_based_pooling = */ true,
      /* enable_alternative_services = */ true, server_ssl_config,
      proxy_ssl_config);
  JobController* job_controller_raw_ptr = job_controller.get();
  job_controller_set_.insert(std::move(job_controller));
  job_controller_raw_ptr->Preconnect(num_streams);
}

const HostMappingRules* HttpStreamFactory::GetHostMappingRules() const {
  return &session_->params().host_mapping_rules;
}

void HttpStreamFactory::OnJobControllerComplete(JobController* controller) {
  auto it = job_controller_set_.find(controller);
  if (it != job_controller_set_.end()) {
    job_controller_set_.erase(it);
  } else {
    NOTREACHED();
  }
}

HttpStreamFactory::PreconnectingProxyServer::PreconnectingProxyServer(
    ProxyServer proxy_server,
    PrivacyMode privacy_mode)
    : proxy_server(proxy_server), privacy_mode(privacy_mode) {}

bool HttpStreamFactory::PreconnectingProxyServer::operator<(
    const PreconnectingProxyServer& other) const {
  return std::tie(proxy_server, privacy_mode) <
         std::tie(other.proxy_server, other.privacy_mode);
}

bool HttpStreamFactory::PreconnectingProxyServer::operator==(
    const PreconnectingProxyServer& other) const {
  return proxy_server == other.proxy_server &&
         privacy_mode == other.privacy_mode;
}

bool HttpStreamFactory::OnInitConnection(const JobController& controller,
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

  if (base::Contains(preconnecting_proxy_servers_,
                     preconnecting_proxy_server)) {
    UMA_HISTOGRAM_EXACT_LINEAR("Net.PreconnectSkippedToProxyServers", 1, 2);
    // Skip preconnect to the proxy server since we are already preconnecting
    // (probably via some other job). See https://crbug.com/682041 for details.
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

void HttpStreamFactory::OnStreamReady(const ProxyInfo& proxy_info,
                                      PrivacyMode privacy_mode) {
  if (proxy_info.is_empty())
    return;
  preconnecting_proxy_servers_.erase(
      PreconnectingProxyServer(proxy_info.proxy_server(), privacy_mode));
}

bool HttpStreamFactory::ProxyServerSupportsPriorities(
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

void HttpStreamFactory::DumpMemoryStats(
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
