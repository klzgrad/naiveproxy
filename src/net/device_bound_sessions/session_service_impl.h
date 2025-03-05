// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DEVICE_BOUND_SESSIONS_SESSION_SERVICE_IMPL_H_
#define NET_DEVICE_BOUND_SESSIONS_SESSION_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/containers/unique_ptr_adapters.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "net/base/net_export.h"
#include "net/device_bound_sessions/registration_fetcher.h"
#include "net/device_bound_sessions/registration_fetcher_param.h"
#include "net/device_bound_sessions/session.h"
#include "net/device_bound_sessions/session_key.h"
#include "net/device_bound_sessions/session_service.h"

namespace net {
class URLRequest;
class URLRequestContext;
class SchemefulSite;
}  // namespace net

namespace unexportable_keys {
class UnexportableKeyService;
}

namespace net::device_bound_sessions {

class SessionStore;

struct DeferredURLRequest {
  DeferredURLRequest(const URLRequest* request,
                     SessionService::RefreshCompleteCallback restart_callback,
                     SessionService::RefreshCompleteCallback continue_callback);
  DeferredURLRequest(DeferredURLRequest&& other) noexcept;

  DeferredURLRequest& operator=(DeferredURLRequest&& other) noexcept;

  ~DeferredURLRequest();

  raw_ptr<const URLRequest> request = nullptr;
  SessionService::RefreshCompleteCallback restart_callback;
  SessionService::RefreshCompleteCallback continue_callback;
};

class NET_EXPORT SessionServiceImpl : public SessionService {
 public:
  SessionServiceImpl(unexportable_keys::UnexportableKeyService& key_service,
                     const URLRequestContext* request_context,
                     SessionStore* store);
  ~SessionServiceImpl() override;

  // Loads saved session data from disk if a `SessionStore` object is provided
  // during construction. Otherwise, it is a no-op.
  void LoadSessionsAsync();

  void RegisterBoundSession(
      OnAccessCallback on_access_callback,
      RegistrationFetcherParam registration_params,
      const IsolationInfo& isolation_info,
      const NetLogWithSource& net_log,
      const std::optional<url::Origin>& original_request_initiator) override;

  std::optional<Session::Id> GetAnySessionRequiringDeferral(
      URLRequest* request) override;

  void DeferRequestForRefresh(
      URLRequest* request,
      Session::Id session_id,
      RefreshCompleteCallback restart_callback,
      RefreshCompleteCallback continue_callback) override;

  void SetChallengeForBoundSession(OnAccessCallback on_access_callback,
                                   const GURL& request_url,
                                   const SessionChallengeParam& param) override;

  void GetAllSessionsAsync(
      base::OnceCallback<void(const std::vector<SessionKey>&)> callback)
      override;
  void DeleteSessionAndNotify(
      const SchemefulSite& site,
      const Session::Id& id,
      SessionService::OnAccessCallback per_request_callback) override;
  void DeleteAllSessions(
      std::optional<base::Time> created_after_time,
      std::optional<base::Time> created_before_time,
      base::RepeatingCallback<bool(const net::SchemefulSite&)> site_matcher,
      base::OnceClosure completion_callback) override;
  base::ScopedClosureRunner AddObserver(
      const GURL& url,
      base::RepeatingCallback<void(const SessionAccess&)> callback) override;
  Session* GetSession(const SchemefulSite& site,
                      const Session::Id& session_id) const;

 private:
  friend class SessionServiceImplWithStoreTest;

  // The key is the site (eTLD+1) of the session's origin.
  using SessionsMap = std::multimap<SchemefulSite, std::unique_ptr<Session>>;
  using DeferredRequestsMap =
      std::unordered_map<Session::Id,
                         absl::InlinedVector<DeferredURLRequest, 1>>;

  struct Observer {
    Observer(const GURL& url,
             base::RepeatingCallback<void(const SessionAccess&)> callback);

    Observer(const Observer&) = delete;
    Observer& operator=(const Observer&) = delete;

    ~Observer();

    GURL url;
    base::RepeatingCallback<void(const SessionAccess&)> callback;
  };

  using ObserverSet =
      std::set<std::unique_ptr<Observer>, base::UniquePtrComparator>;

  void OnLoadSessionsComplete(SessionsMap sessions);

  void OnRegistrationComplete(
      OnAccessCallback on_access_callback,
      std::optional<RegistrationFetcher::RegistrationCompleteParams> params);
  void OnRefreshRequestCompletion(
      OnAccessCallback on_access_callback,
      SchemefulSite site,
      Session::Id session_id,
      std::optional<RegistrationFetcher::RegistrationCompleteParams> result);

  void AddSession(const SchemefulSite& site, std::unique_ptr<Session> session);
  void UnblockDeferredRequests(const Session::Id& session_id,
                               bool is_cookie_refreshed);

  // Get all the unexpired sessions for a given site. This also removes
  // expired sessions for the site and extends the TTL of used sessions.
  std::pair<SessionsMap::iterator, SessionsMap::iterator> GetSessionsForSite(
      const SchemefulSite& site);

  // Remove a session from the session map. It also clears the session
  // from `session_store_` and notifies any observers (including
  // `per_request_callback`) about the termination.
  // Return the iterator to the next session in the map.
  [[nodiscard]] SessionsMap::iterator DeleteSessionAndNotifyInternal(
      SessionsMap::iterator it,
      SessionService::OnAccessCallback per_request_callback);

  // Notify all observers about an access to a session. Will update
  // `per_request_callback` unconditionally, and any observers in
  // `observers_` which have a URL in the scope of `session`.
  void NotifySessionAccess(
      SessionService::OnAccessCallback per_request_callback,
      SessionAccess::AccessType access_type,
      const SchemefulSite& site,
      const Session& session);

  // Remove an observer by site and pointer.
  void RemoveObserver(net::SchemefulSite site, Observer* observer);

  // Whether we are waiting on the initial load of saved sessions to complete.
  bool pending_initialization_ = false;
  // Functions to call once initialization completes.
  std::vector<base::OnceClosure> queued_operations_;

  const raw_ref<unexportable_keys::UnexportableKeyService> key_service_;
  raw_ptr<const URLRequestContext> context_;
  raw_ptr<SessionStore> session_store_ = nullptr;

  // Deferred requests are stored by session ID.
  DeferredRequestsMap deferred_requests_;

  // Storage is similar to how CookieMonster stores its cookies.
  SessionsMap unpartitioned_sessions_;

  // All observers of sessions.
  std::map<net::SchemefulSite, ObserverSet> observers_by_site_;

  base::WeakPtrFactory<SessionServiceImpl> weak_factory_{this};
};

}  // namespace net::device_bound_sessions

#endif  // NET_DEVICE_BOUND_SESSIONS_SESSION_SERVICE_IMPL_H_
