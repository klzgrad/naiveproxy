// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_SERVER_PROPERTIES_IMPL_H_
#define NET_HTTP_HTTP_SERVER_PROPERTIES_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/time/default_tick_clock.h"
#include "base/values.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/linked_hash_map.h"
#include "net/base/net_export.h"
#include "net/http/broken_alternative_services.h"
#include "net/http/http_server_properties.h"

namespace base {
class TickClock;
}

namespace net {

// The implementation for setting/retrieving the HTTP server properties.
class NET_EXPORT HttpServerPropertiesImpl
    : public HttpServerProperties,
      public BrokenAlternativeServices::Delegate {
 public:
  // |clock| is used for setting expiration times and scheduling the
  // expiration of broken alternative services. If null, default clock will be
  // used.
  explicit HttpServerPropertiesImpl(base::TickClock* clock);

  // Default clock will be used.
  HttpServerPropertiesImpl();

  ~HttpServerPropertiesImpl() override;

  // Sets |spdy_servers_map_| with the servers (host/port) from
  // |spdy_servers| that either support SPDY or not.
  void SetSpdyServers(std::unique_ptr<SpdyServersMap> spdy_servers_map);

  void SetAlternativeServiceServers(
      std::unique_ptr<AlternativeServiceMap> alternate_protocol_servers);

  void SetSupportsQuic(const IPAddress& last_address);

  void SetServerNetworkStats(
      std::unique_ptr<ServerNetworkStatsMap> server_network_stats_map);

  void SetQuicServerInfoMap(
      std::unique_ptr<QuicServerInfoMap> quic_server_info_map);

  // Get the list of servers (host/port) that support SPDY. The max_size is the
  // number of MRU servers that support SPDY that are to be returned.
  void GetSpdyServerList(std::vector<std::string>* spdy_servers,
                         size_t max_size) const;

  void SetBrokenAndRecentlyBrokenAlternativeServices(
      std::unique_ptr<BrokenAlternativeServiceList>
          broken_alternative_service_list,
      std::unique_ptr<RecentlyBrokenAlternativeServices>
          recently_broken_alternative_services);

  const BrokenAlternativeServiceList& broken_alternative_service_list() const;

  const RecentlyBrokenAlternativeServices&
  recently_broken_alternative_services() const;

  // Returns flattened string representation of the |host_port_pair|. Used by
  // unittests.
  static std::string GetFlattenedSpdyServer(const HostPortPair& host_port_pair);

  // Returns the canonical host suffix for |host|, or nullptr if none
  // exists.
  const std::string* GetCanonicalSuffix(const std::string& host) const;

  // -----------------------------
  // HttpServerProperties methods:
  // -----------------------------

  void Clear() override;
  bool SupportsRequestPriority(const url::SchemeHostPort& server) override;
  bool GetSupportsSpdy(const url::SchemeHostPort& server) override;
  void SetSupportsSpdy(const url::SchemeHostPort& server,
                       bool support_spdy) override;
  bool RequiresHTTP11(const HostPortPair& server) override;
  void SetHTTP11Required(const HostPortPair& server) override;
  void MaybeForceHTTP11(const HostPortPair& server,
                        SSLConfig* ssl_config) override;
  AlternativeServiceInfoVector GetAlternativeServiceInfos(
      const url::SchemeHostPort& origin) override;
  bool SetHttp2AlternativeService(const url::SchemeHostPort& origin,
                                  const AlternativeService& alternative_service,
                                  base::Time expiration) override;
  bool SetQuicAlternativeService(
      const url::SchemeHostPort& origin,
      const AlternativeService& alternative_service,
      base::Time expiration,
      const QuicTransportVersionVector& advertised_versions) override;
  bool SetAlternativeServices(const url::SchemeHostPort& origin,
                              const AlternativeServiceInfoVector&
                                  alternative_service_info_vector) override;
  void MarkAlternativeServiceBroken(
      const AlternativeService& alternative_service) override;
  void MarkAlternativeServiceRecentlyBroken(
      const AlternativeService& alternative_service) override;
  bool IsAlternativeServiceBroken(
      const AlternativeService& alternative_service) const override;
  bool WasAlternativeServiceRecentlyBroken(
      const AlternativeService& alternative_service) override;
  void ConfirmAlternativeService(
      const AlternativeService& alternative_service) override;
  const AlternativeServiceMap& alternative_service_map() const override;
  std::unique_ptr<base::Value> GetAlternativeServiceInfoAsValue()
      const override;
  bool GetSupportsQuic(IPAddress* last_address) const override;
  void SetSupportsQuic(bool used_quic, const IPAddress& address) override;
  void SetServerNetworkStats(const url::SchemeHostPort& server,
                             ServerNetworkStats stats) override;
  void ClearServerNetworkStats(const url::SchemeHostPort& server) override;
  const ServerNetworkStats* GetServerNetworkStats(
      const url::SchemeHostPort& server) override;
  const ServerNetworkStatsMap& server_network_stats_map() const override;
  bool SetQuicServerInfo(const QuicServerId& server_id,
                         const std::string& server_info) override;
  const std::string* GetQuicServerInfo(const QuicServerId& server_id) override;
  const QuicServerInfoMap& quic_server_info_map() const override;
  size_t max_server_configs_stored_in_properties() const override;
  void SetMaxServerConfigsStoredInProperties(
      size_t max_server_configs_stored_in_properties) override;
  bool IsInitialized() const override;

  // BrokenAlternativeServices::Delegate method.
  void OnExpireBrokenAlternativeService(
      const AlternativeService& expired_alternative_service) override;

 private:
  // TODO (wangyix): modify HttpServerPropertiesImpl unit tests so this
  // friendness is no longer required.
  friend class HttpServerPropertiesImplPeer;

  typedef std::map<url::SchemeHostPort, url::SchemeHostPort> CanonicalHostMap;
  typedef std::vector<std::string> CanonicalSufficList;
  typedef std::set<HostPortPair> Http11ServerHostPortSet;

  // Return the iterator for |server|, or for its canonical host, or end.
  AlternativeServiceMap::const_iterator GetAlternateProtocolIterator(
      const url::SchemeHostPort& server);

  // Return the canonical host for |server|, or end if none exists.
  CanonicalHostMap::const_iterator GetCanonicalHost(
      const url::SchemeHostPort& server) const;

  // Remove the cononical host for |server|.
  void RemoveCanonicalHost(const url::SchemeHostPort& server);

  base::DefaultTickClock default_clock_;

  SpdyServersMap spdy_servers_map_;
  Http11ServerHostPortSet http11_servers_;

  AlternativeServiceMap alternative_service_map_;

  BrokenAlternativeServices broken_alternative_services_;

  IPAddress last_quic_address_;
  ServerNetworkStatsMap server_network_stats_map_;
  // Contains a map of servers which could share the same alternate protocol.
  // Map from a Canonical scheme/host/port (host is some postfix of host names)
  // to an actual origin, which has a plausible alternate protocol mapping.
  CanonicalHostMap canonical_host_to_origin_map_;
  // Contains list of suffixes (for exmaple ".c.youtube.com",
  // ".googlevideo.com", ".googleusercontent.com") of canonical hostnames.
  CanonicalSufficList canonical_suffixes_;

  QuicServerInfoMap quic_server_info_map_;
  size_t max_server_configs_stored_in_properties_;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(HttpServerPropertiesImpl);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_SERVER_PROPERTIES_IMPL_H_
