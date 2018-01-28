// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/spdy_session_pool.h"

#include <utility>

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "net/base/address_list.h"
#include "net/base/trace_constants.h"
#include "net/http/http_network_session.h"
#include "net/http/http_server_properties.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_with_source.h"
#include "net/spdy/chromium/bidirectional_stream_spdy_impl.h"
#include "net/spdy/chromium/spdy_http_stream.h"
#include "net/spdy/chromium/spdy_session.h"
#include "net/spdy/core/hpack/hpack_constants.h"
#include "net/spdy/core/hpack/hpack_huffman_table.h"
#include "net/spdy/core/hpack/hpack_static_table.h"
#include "net/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "net/spdy/platform/api/spdy_string_utils.h"

namespace net {

namespace {

enum SpdySessionGetTypes {
  CREATED_NEW                 = 0,
  FOUND_EXISTING              = 1,
  FOUND_EXISTING_FROM_IP_POOL = 2,
  IMPORTED_FROM_SOCKET        = 3,
  SPDY_SESSION_GET_MAX        = 4
};

}  // namespace

SpdySessionPool::SpdySessionPool(
    HostResolver* resolver,
    SSLConfigService* ssl_config_service,
    HttpServerProperties* http_server_properties,
    TransportSecurityState* transport_security_state,
    const QuicTransportVersionVector& quic_supported_versions,
    bool enable_ping_based_connection_checking,
    size_t session_max_recv_window_size,
    const SettingsMap& initial_settings,
    SpdySessionPool::TimeFunc time_func,
    ProxyDelegate* proxy_delegate)
    : http_server_properties_(http_server_properties),
      transport_security_state_(transport_security_state),
      ssl_config_service_(ssl_config_service),
      resolver_(resolver),
      quic_supported_versions_(quic_supported_versions),
      enable_sending_initial_data_(true),
      enable_ping_based_connection_checking_(
          enable_ping_based_connection_checking),
      session_max_recv_window_size_(session_max_recv_window_size),
      initial_settings_(initial_settings),
      time_func_(time_func),
      push_delegate_(nullptr),
      proxy_delegate_(proxy_delegate) {
  NetworkChangeNotifier::AddIPAddressObserver(this);
  if (ssl_config_service_.get())
    ssl_config_service_->AddObserver(this);
  CertDatabase::GetInstance()->AddObserver(this);
}

SpdySessionPool::~SpdySessionPool() {
  DCHECK(spdy_session_request_map_.empty());
  CloseAllSessions();

  while (!sessions_.empty()) {
    // Destroy sessions to enforce that lifetime is scoped to SpdySessionPool.
    // Write callbacks queued upon session drain are not invoked.
    RemoveUnavailableSession((*sessions_.begin())->GetWeakPtr());
  }

  if (ssl_config_service_.get())
    ssl_config_service_->RemoveObserver(this);
  NetworkChangeNotifier::RemoveIPAddressObserver(this);
  CertDatabase::GetInstance()->RemoveObserver(this);
}

base::WeakPtr<SpdySession> SpdySessionPool::CreateAvailableSessionFromSocket(
    const SpdySessionKey& key,
    std::unique_ptr<ClientSocketHandle> connection,
    const NetLogWithSource& net_log) {
  TRACE_EVENT0(kNetTracingCategory,
               "SpdySessionPool::CreateAvailableSessionFromSocket");

  UMA_HISTOGRAM_ENUMERATION(
      "Net.SpdySessionGet", IMPORTED_FROM_SOCKET, SPDY_SESSION_GET_MAX);

  auto new_session = std::make_unique<SpdySession>(
      key, http_server_properties_, transport_security_state_,
      quic_supported_versions_, enable_sending_initial_data_,
      enable_ping_based_connection_checking_, session_max_recv_window_size_,
      initial_settings_, time_func_, push_delegate_, proxy_delegate_,
      net_log.net_log());

  new_session->InitializeWithSocket(std::move(connection), this);

  base::WeakPtr<SpdySession> available_session = new_session->GetWeakPtr();
  sessions_.insert(new_session.release());
  MapKeyToAvailableSession(key, available_session);

  net_log.AddEvent(
      NetLogEventType::HTTP2_SESSION_POOL_IMPORTED_SESSION_FROM_SOCKET,
      available_session->net_log().source().ToEventParametersCallback());

  // Look up the IP address for this session so that we can match
  // future sessions (potentially to different domains) which can
  // potentially be pooled with this one. Because GetPeerAddress()
  // reports the proxy's address instead of the origin server, check
  // to see if this is a direct connection.
  if (key.proxy_server().is_direct()) {
    IPEndPoint address;
    if (available_session->GetPeerAddress(&address) == OK)
      aliases_[address] = key;
  }

  return available_session;
}

base::WeakPtr<SpdySession> SpdySessionPool::FindAvailableSession(
    const SpdySessionKey& key,
    bool enable_ip_based_pooling,
    const NetLogWithSource& net_log) {
  AvailableSessionMap::iterator it = LookupAvailableSessionByKey(key);
  if (it != available_sessions_.end()) {
    if (key.Equals(it->second->spdy_session_key())) {
      UMA_HISTOGRAM_ENUMERATION("Net.SpdySessionGet", FOUND_EXISTING,
                                SPDY_SESSION_GET_MAX);
      net_log.AddEvent(
          NetLogEventType::HTTP2_SESSION_POOL_FOUND_EXISTING_SESSION,
          it->second->net_log().source().ToEventParametersCallback());
    } else {
      if (!enable_ip_based_pooling) {
        // Remove session from available sessions and from aliases, and remove
        // key from the session's pooled alias set, so that a new session can be
        // created with this |key|.
        it->second->RemovePooledAlias(key);
        UnmapKey(key);
        RemoveAliases(key);
        return base::WeakPtr<SpdySession>();
      }

      UMA_HISTOGRAM_ENUMERATION("Net.SpdySessionGet",
                                FOUND_EXISTING_FROM_IP_POOL,
                                SPDY_SESSION_GET_MAX);
      net_log.AddEvent(
          NetLogEventType::
              HTTP2_SESSION_POOL_FOUND_EXISTING_SESSION_FROM_IP_POOL,
          it->second->net_log().source().ToEventParametersCallback());
    }
    return it->second;
  }

  if (!enable_ip_based_pooling)
    return base::WeakPtr<SpdySession>();

  // Look up IP addresses from resolver cache.
  HostResolver::RequestInfo resolve_info(key.host_port_pair());
  AddressList addresses;
  int rv = resolver_->ResolveFromCache(resolve_info, &addresses, net_log);
  DCHECK_NE(rv, ERR_IO_PENDING);
  if (rv != OK)
    return base::WeakPtr<SpdySession>();

  // Check if we have a session through a domain alias.
  for (AddressList::const_iterator address_it = addresses.begin();
       address_it != addresses.end();
       ++address_it) {
    AliasMap::const_iterator alias_it = aliases_.find(*address_it);
    if (alias_it == aliases_.end())
      continue;

    // We found an alias.
    const SpdySessionKey& alias_key = alias_it->second;

    // We can reuse this session only if the proxy and privacy
    // settings match.
    if (!(alias_key.proxy_server() == key.proxy_server()) ||
        !(alias_key.privacy_mode() == key.privacy_mode()))
      continue;

    AvailableSessionMap::iterator available_session_it =
        LookupAvailableSessionByKey(alias_key);
    if (available_session_it == available_sessions_.end()) {
      NOTREACHED();  // It shouldn't be in the aliases table if we can't get it!
      continue;
    }

    const base::WeakPtr<SpdySession>& available_session =
        available_session_it->second;
    DCHECK(base::ContainsKey(sessions_, available_session.get()));
    // If the session is a secure one, we need to verify that the
    // server is authenticated to serve traffic for |host_port_proxy_pair| too.
    if (!available_session->VerifyDomainAuthentication(
            key.host_port_pair().host())) {
      UMA_HISTOGRAM_ENUMERATION("Net.SpdyIPPoolDomainMatch", 0, 2);
      continue;
    }

    UMA_HISTOGRAM_ENUMERATION("Net.SpdyIPPoolDomainMatch", 1, 2);
    UMA_HISTOGRAM_ENUMERATION("Net.SpdySessionGet",
                              FOUND_EXISTING_FROM_IP_POOL,
                              SPDY_SESSION_GET_MAX);
    net_log.AddEvent(
        NetLogEventType::HTTP2_SESSION_POOL_FOUND_EXISTING_SESSION_FROM_IP_POOL,
        available_session->net_log().source().ToEventParametersCallback());
    // Add this session to the map so that we can find it next time.
    MapKeyToAvailableSession(key, available_session);
    available_session->AddPooledAlias(key);
    return available_session;
  }

  return base::WeakPtr<SpdySession>();
}

void SpdySessionPool::MakeSessionUnavailable(
    const base::WeakPtr<SpdySession>& available_session) {
  UnmapKey(available_session->spdy_session_key());
  RemoveAliases(available_session->spdy_session_key());
  const std::set<SpdySessionKey>& aliases = available_session->pooled_aliases();
  for (std::set<SpdySessionKey>::const_iterator it = aliases.begin();
       it != aliases.end(); ++it) {
    UnmapKey(*it);
    RemoveAliases(*it);
  }
  DCHECK(!IsSessionAvailable(available_session));
}

void SpdySessionPool::RemoveUnavailableSession(
    const base::WeakPtr<SpdySession>& unavailable_session) {
  DCHECK(!IsSessionAvailable(unavailable_session));

  unavailable_session->net_log().AddEvent(
      NetLogEventType::HTTP2_SESSION_POOL_REMOVE_SESSION);

  SessionSet::iterator it = sessions_.find(unavailable_session.get());
  CHECK(it != sessions_.end());
  std::unique_ptr<SpdySession> owned_session(*it);
  sessions_.erase(it);
}

// Make a copy of |sessions_| in the Close* functions below to avoid
// reentrancy problems. Since arbitrary functions get called by close
// handlers, it doesn't suffice to simply increment the iterator
// before closing.

void SpdySessionPool::CloseCurrentSessions(Error error) {
  CloseCurrentSessionsHelper(error, "Closing current sessions.",
                             false /* idle_only */);
}

void SpdySessionPool::CloseCurrentIdleSessions() {
  CloseCurrentSessionsHelper(ERR_ABORTED, "Closing idle sessions.",
                             true /* idle_only */);
}

void SpdySessionPool::CloseAllSessions() {
  while (!available_sessions_.empty()) {
    CloseCurrentSessionsHelper(ERR_ABORTED, "Closing all sessions.",
                               false /* idle_only */);
  }
}

std::unique_ptr<base::Value> SpdySessionPool::SpdySessionPoolInfoToValue()
    const {
  auto list = std::make_unique<base::ListValue>();

  for (AvailableSessionMap::const_iterator it = available_sessions_.begin();
       it != available_sessions_.end(); ++it) {
    // Only add the session if the key in the map matches the main
    // host_port_proxy_pair (not an alias).
    const SpdySessionKey& key = it->first;
    const SpdySessionKey& session_key = it->second->spdy_session_key();
    if (key.Equals(session_key))
      list->Append(it->second->GetInfoAsValue());
  }
  return std::move(list);
}

void SpdySessionPool::OnIPAddressChanged() {
  WeakSessionList current_sessions = GetCurrentSessions();
  for (WeakSessionList::const_iterator it = current_sessions.begin();
       it != current_sessions.end(); ++it) {
    if (!*it)
      continue;

// For OSs that terminate TCP connections upon relevant network changes,
// attempt to preserve active streams by marking all sessions as going
// away, rather than explicitly closing them. Streams may still fail due
// to a generated TCP reset.
#if defined(OS_ANDROID) || defined(OS_WIN) || defined(OS_IOS)
    (*it)->MakeUnavailable();
    (*it)->StartGoingAway(kLastStreamId, ERR_NETWORK_CHANGED);
    (*it)->MaybeFinishGoingAway();
#else
    (*it)->CloseSessionOnError(ERR_NETWORK_CHANGED,
                               "Closing current sessions.");
    DCHECK((*it)->IsDraining());
#endif  // defined(OS_ANDROID) || defined(OS_WIN) || defined(OS_IOS)
    DCHECK(!IsSessionAvailable(*it));
  }
}

void SpdySessionPool::OnSSLConfigChanged() {
  CloseCurrentSessions(ERR_NETWORK_CHANGED);
}

void SpdySessionPool::OnCertDBChanged() {
  CloseCurrentSessions(ERR_CERT_DATABASE_CHANGED);
}

void SpdySessionPool::OnNewSpdySessionReady(
    const base::WeakPtr<SpdySession>& spdy_session,
    bool direct,
    const SSLConfig& used_ssl_config,
    const ProxyInfo& used_proxy_info,
    bool was_alpn_negotiated,
    NextProto negotiated_protocol,
    bool using_spdy,
    NetLogSource source_dependency) {
  while (spdy_session) {
    const SpdySessionKey& spdy_session_key = spdy_session->spdy_session_key();
    // Each iteration may empty out the RequestSet for |spdy_session_key| in
    // |spdy_session_request_map_|. So each time, check for RequestSet and use
    // the first one.
    //
    // TODO(willchan): If it's important, switch RequestSet out for a FIFO
    // queue (Order by priority first, then FIFO within same priority). Unclear
    // that it matters here.
    auto iter = spdy_session_request_map_.find(spdy_session_key);
    if (iter == spdy_session_request_map_.end())
      return;
    HttpStreamFactoryImpl::Request* request = *iter->second.begin();
    request->Complete(was_alpn_negotiated, negotiated_protocol, using_spdy);
    RemoveRequestFromSpdySessionRequestMap(request);
    if (request->stream_type() == HttpStreamRequest::BIDIRECTIONAL_STREAM) {
      request->OnBidirectionalStreamImplReadyOnPooledConnection(
          used_ssl_config, used_proxy_info,
          std::make_unique<BidirectionalStreamSpdyImpl>(spdy_session,
                                                        source_dependency));
    } else {
      bool use_relative_url =
          direct || request->url().SchemeIs(url::kHttpsScheme);
      request->OnStreamReadyOnPooledConnection(
          used_ssl_config, used_proxy_info,
          std::make_unique<SpdyHttpStream>(spdy_session, use_relative_url,
                                           source_dependency));
    }
  }
  // TODO(mbelshe): Alert other valid requests.
}

bool SpdySessionPool::StartRequest(const SpdySessionKey& spdy_session_key,
                                   const base::Closure& callback) {
  auto iter = spdy_session_pending_request_map_.find(spdy_session_key);
  if (iter == spdy_session_pending_request_map_.end()) {
    spdy_session_pending_request_map_.emplace(spdy_session_key,
                                              std::list<base::Closure>{});
    return true;
  }
  iter->second.push_back(callback);
  return false;
}

void SpdySessionPool::ResumePendingRequests(
    const SpdySessionKey& spdy_session_key) {
  auto iter = spdy_session_pending_request_map_.find(spdy_session_key);
  if (iter != spdy_session_pending_request_map_.end()) {
    for (auto callback : iter->second) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, callback);
    }
    spdy_session_pending_request_map_.erase(iter);
  }
}

void SpdySessionPool::AddRequestToSpdySessionRequestMap(
    const SpdySessionKey& spdy_session_key,
    HttpStreamFactoryImpl::Request* request) {
  if (request->HasSpdySessionKey())
    return;
  RequestSet& request_set = spdy_session_request_map_[spdy_session_key];
  DCHECK(!base::ContainsKey(request_set, request));
  request_set.insert(request);
  request->SetSpdySessionKey(spdy_session_key);
}

void SpdySessionPool::RemoveRequestFromSpdySessionRequestMap(
    HttpStreamFactoryImpl::Request* request) {
  if (!request->HasSpdySessionKey())
    return;
  const SpdySessionKey& spdy_session_key = request->GetSpdySessionKey();
  // Resume all pending requests now that |request| is done/canceled.
  ResumePendingRequests(spdy_session_key);

  auto iter = spdy_session_request_map_.find(spdy_session_key);
  DCHECK(iter != spdy_session_request_map_.end());
  RequestSet& request_set = iter->second;
  DCHECK(base::ContainsKey(request_set, request));
  request_set.erase(request);
  if (request_set.empty())
    spdy_session_request_map_.erase(spdy_session_key);
  // Resets |request|'s SpdySessionKey. This will invalid |spdy_session_key|.
  request->ResetSpdySessionKey();
}

void SpdySessionPool::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const SpdyString& parent_dump_absolute_name) const {
  if (sessions_.empty())
    return;
  size_t total_size = 0;
  size_t buffer_size = 0;
  size_t cert_count = 0;
  size_t cert_size = 0;
  size_t num_active_sessions = 0;
  for (auto* session : sessions_) {
    StreamSocket::SocketMemoryStats stats;
    bool is_session_active = false;
    total_size += session->DumpMemoryStats(&stats, &is_session_active);
    buffer_size += stats.buffer_size;
    cert_count += stats.cert_count;
    cert_size += stats.cert_size;
    if (is_session_active)
      num_active_sessions++;
  }
  total_size += SpdyEstimateMemoryUsage(ObtainHpackHuffmanTable()) +
                SpdyEstimateMemoryUsage(ObtainHpackStaticTable());
  base::trace_event::MemoryAllocatorDump* dump =
      pmd->CreateAllocatorDump(SpdyStringPrintf(
          "%s/spdy_session_pool", parent_dump_absolute_name.c_str()));
  dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameSize,
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                  total_size);
  dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameObjectCount,
                  base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                  sessions_.size());
  dump->AddScalar("active_session_count",
                  base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                  num_active_sessions);
  dump->AddScalar("buffer_size",
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                  buffer_size);
  dump->AddScalar("cert_count",
                  base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                  cert_count);
  dump->AddScalar("cert_size",
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                  cert_size);
}

bool SpdySessionPool::IsSessionAvailable(
    const base::WeakPtr<SpdySession>& session) const {
  for (AvailableSessionMap::const_iterator it = available_sessions_.begin();
       it != available_sessions_.end(); ++it) {
    if (it->second.get() == session.get())
      return true;
  }
  return false;
}

void SpdySessionPool::MapKeyToAvailableSession(
    const SpdySessionKey& key,
    const base::WeakPtr<SpdySession>& session) {
  DCHECK(base::ContainsKey(sessions_, session.get()));
  std::pair<AvailableSessionMap::iterator, bool> result =
      available_sessions_.insert(std::make_pair(key, session));
  CHECK(result.second);
}

SpdySessionPool::AvailableSessionMap::iterator
SpdySessionPool::LookupAvailableSessionByKey(
    const SpdySessionKey& key) {
  return available_sessions_.find(key);
}

void SpdySessionPool::UnmapKey(const SpdySessionKey& key) {
  AvailableSessionMap::iterator it = LookupAvailableSessionByKey(key);
  CHECK(it != available_sessions_.end());
  available_sessions_.erase(it);
}

void SpdySessionPool::RemoveAliases(const SpdySessionKey& key) {
  // Walk the aliases map, find references to this pair.
  // TODO(mbelshe):  Figure out if this is too expensive.
  for (AliasMap::iterator it = aliases_.begin(); it != aliases_.end(); ) {
    if (it->second.Equals(key)) {
      AliasMap::iterator old_it = it;
      ++it;
      aliases_.erase(old_it);
    } else {
      ++it;
    }
  }
}

SpdySessionPool::WeakSessionList SpdySessionPool::GetCurrentSessions() const {
  WeakSessionList current_sessions;
  for (SessionSet::const_iterator it = sessions_.begin();
       it != sessions_.end(); ++it) {
    current_sessions.push_back((*it)->GetWeakPtr());
  }
  return current_sessions;
}

void SpdySessionPool::CloseCurrentSessionsHelper(Error error,
                                                 const SpdyString& description,
                                                 bool idle_only) {
  WeakSessionList current_sessions = GetCurrentSessions();
  for (WeakSessionList::const_iterator it = current_sessions.begin();
       it != current_sessions.end(); ++it) {
    if (!*it)
      continue;

    if (idle_only && (*it)->is_active())
      continue;

    (*it)->CloseSessionOnError(error, description);
    DCHECK(!IsSessionAvailable(*it));
  }
}

}  // namespace net
