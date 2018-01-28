// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_STREAM_FACTORY_IMPL_H_
#define NET_HTTP_HTTP_STREAM_FACTORY_IMPL_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <set>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/base/privacy_mode.h"
#include "net/base/request_priority.h"
#include "net/http/http_stream_factory.h"
#include "net/log/net_log_source.h"
#include "net/proxy/proxy_server.h"
#include "net/socket/ssl_client_socket.h"
#include "net/spdy/chromium/spdy_session_key.h"

namespace net {

class HttpNetworkSession;
class ProxyInfo;
class NetLogWithSource;

class NET_EXPORT_PRIVATE HttpStreamFactoryImpl : public HttpStreamFactory {
 public:
  class NET_EXPORT_PRIVATE Job;
  class NET_EXPORT_PRIVATE JobController;
  class NET_EXPORT_PRIVATE JobFactory;
  class NET_EXPORT_PRIVATE Request;
  // RequestStream may only be called if |for_websockets| is false.
  // RequestWebSocketHandshakeStream may only be called if |for_websockets|
  // is true.
  HttpStreamFactoryImpl(HttpNetworkSession* session, bool for_websockets);
  ~HttpStreamFactoryImpl() override;

  // HttpStreamFactory interface
  std::unique_ptr<HttpStreamRequest> RequestStream(
      const HttpRequestInfo& info,
      RequestPriority priority,
      const SSLConfig& server_ssl_config,
      const SSLConfig& proxy_ssl_config,
      HttpStreamRequest::Delegate* delegate,
      bool enable_ip_based_pooling,
      bool enable_alternative_services,
      const NetLogWithSource& net_log) override;

  std::unique_ptr<HttpStreamRequest> RequestWebSocketHandshakeStream(
      const HttpRequestInfo& info,
      RequestPriority priority,
      const SSLConfig& server_ssl_config,
      const SSLConfig& proxy_ssl_config,
      HttpStreamRequest::Delegate* delegate,
      WebSocketHandshakeStreamBase::CreateHelper* create_helper,
      bool enable_ip_based_pooling,
      bool enable_alternative_services,
      const NetLogWithSource& net_log) override;

  std::unique_ptr<HttpStreamRequest> RequestBidirectionalStreamImpl(
      const HttpRequestInfo& info,
      RequestPriority priority,
      const SSLConfig& server_ssl_config,
      const SSLConfig& proxy_ssl_config,
      HttpStreamRequest::Delegate* delegate,
      bool enable_ip_based_pooling,
      bool enable_alternative_services,
      const NetLogWithSource& net_log) override;

  void PreconnectStreams(int num_streams, const HttpRequestInfo& info) override;
  const HostMappingRules* GetHostMappingRules() const override;
  void DumpMemoryStats(base::trace_event::ProcessMemoryDump* pmd,
                       const std::string& parent_absolute_name) const override;

  enum JobType {
    MAIN,
    ALTERNATIVE,
    PRECONNECT,
  };

 private:
  FRIEND_TEST_ALL_PREFIXES(HttpStreamFactoryImplRequestTest, SetPriority);
  FRIEND_TEST_ALL_PREFIXES(HttpStreamFactoryImplRequestTest, DelayMainJob);

  friend class HttpStreamFactoryImplPeer;

  typedef std::set<std::unique_ptr<JobController>> JobControllerSet;

  // |PreconnectingProxyServer| holds information of a connection to a single
  // proxy server.
  struct PreconnectingProxyServer {
    PreconnectingProxyServer(ProxyServer proxy_server,
                             PrivacyMode privacy_mode);

    // Needed to be an element of std::set.
    bool operator<(const PreconnectingProxyServer& other) const;
    bool operator==(const PreconnectingProxyServer& other) const;

    const ProxyServer proxy_server;
    const PrivacyMode privacy_mode;
  };

  // Values must not be changed or reused.  Keep in sync with identically named
  // enum in histograms.xml.
  enum AlternativeServiceType {
    NO_ALTERNATIVE_SERVICE = 0,
    QUIC_SAME_DESTINATION = 1,
    QUIC_DIFFERENT_DESTINATION = 2,
    NOT_QUIC_SAME_DESTINATION = 3,
    NOT_QUIC_DIFFERENT_DESTINATION = 4,
    MAX_ALTERNATIVE_SERVICE_TYPE
  };

  std::unique_ptr<HttpStreamRequest> RequestStreamInternal(
      const HttpRequestInfo& info,
      RequestPriority priority,
      const SSLConfig& server_ssl_config,
      const SSLConfig& proxy_ssl_config,
      HttpStreamRequest::Delegate* delegate,
      WebSocketHandshakeStreamBase::CreateHelper* create_helper,
      HttpStreamRequest::StreamType stream_type,
      bool enable_ip_based_pooling,
      bool enable_alternative_services,
      const NetLogWithSource& net_log);

  // Called when the Job detects that the endpoint indicated by the
  // Alternate-Protocol does not work. Lets the factory update
  // HttpAlternateProtocols with the failure and resets the SPDY session key.
  void OnBrokenAlternateProtocol(const Job*, const HostPortPair& origin);

  // Called when the Preconnect completes. Used for testing.
  virtual void OnPreconnectsCompleteInternal() {}

  // Called when the JobController finishes service. Delete the JobController
  // from |job_controller_set_|.
  void OnJobControllerComplete(JobController* controller);

  // Returns true if a connection to the proxy server contained in |proxy_info|
  // that has privacy mode |privacy_mode| can be skipped by a job controlled by
  // |controller|.
  bool OnInitConnection(const JobController& controller,
                        const ProxyInfo& proxy_info,
                        PrivacyMode privacy_mode);

  // Notifies |this| that a stream to the proxy server contained in |proxy_info|
  // with privacy mode |privacy_mode| is ready.
  void OnStreamReady(const ProxyInfo& proxy_info, PrivacyMode privacy_mode);

  // Returns true if |proxy_info| contains a proxy server that supports request
  // priorities.
  bool ProxyServerSupportsPriorities(const ProxyInfo& proxy_info) const;

  // Adds the count of JobControllers that are not completed to UMA histogram if
  // the count is a multiple of 100: 100, 200, 400, etc. Break down
  // JobControllers count based on the type of JobController.
  void AddJobControllerCountToHistograms();

  HttpNetworkSession* const session_;

  // All Requests/Preconnects are assigned with a JobController to manage
  // serving Job(s). JobController might outlive Request when Request
  // is served while there's some working Job left. JobController will be
  // deleted from |job_controller_set_| when it determines the completion of
  // its work.
  JobControllerSet job_controller_set_;

  // Factory used by job controllers for creating jobs.
  std::unique_ptr<JobFactory> job_factory_;

  // Set of proxy servers that support request priorities to which subsequent
  // preconnects should be skipped.
  std::set<PreconnectingProxyServer> preconnecting_proxy_servers_;

  const bool for_websockets_;

  // The count of JobControllers that was most recently logged to histograms.
  size_t last_logged_job_controller_count_;

  DISALLOW_COPY_AND_ASSIGN(HttpStreamFactoryImpl);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_STREAM_FACTORY_IMPL_H_
