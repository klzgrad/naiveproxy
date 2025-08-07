// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/device_bound_sessions/session_service_impl.h"

#include "base/containers/contains.h"
#include "base/containers/to_vector.h"
#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/task/sequenced_task_runner.h"
#include "components/unexportable_keys/unexportable_key_service.h"
#include "net/base/features.h"
#include "net/base/schemeful_site.h"
#include "net/device_bound_sessions/registration_request_param.h"
#include "net/device_bound_sessions/session_store.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"

namespace net::device_bound_sessions {

namespace {

// Parameters for the refresh quota. We currently allow 2 refreshes in 5
// minutes. This allows sites to refresh every 5 minutes with some error
// tolerance (e.g. a failed refresh or user cookie clearing).
constexpr size_t kRefreshQuota = 2;
constexpr base::TimeDelta kRefreshQuotaInterval = base::Minutes(5);

bool SessionMatchesFilter(
    const SchemefulSite& site,
    const Session& session,
    std::optional<base::Time> created_after_time,
    std::optional<base::Time> created_before_time,
    base::RepeatingCallback<bool(const url::Origin&, const net::SchemefulSite&)>
        origin_and_site_matcher) {
  if (created_before_time && *created_before_time < session.creation_date()) {
    return false;
  }

  if (created_after_time && *created_after_time > session.creation_date()) {
    return false;
  }

  if (!origin_and_site_matcher.is_null() &&
      !origin_and_site_matcher.Run(session.origin(), site)) {
    return false;
  }

  return true;
}

class DebugHeaderBuilder {
 public:
  void AddSkippedSession(SessionKey key, SessionService::RefreshResult result) {
    structured_headers::Item item;
    switch (result) {
      case SessionService::RefreshResult::kRefreshed:
      case SessionService::RefreshResult::kFatalError:
        return;
      case SessionService::RefreshResult::kInitializedService:
        NOTREACHED();
      case SessionService::RefreshResult::kUnreachable:
        item = structured_headers::Item("unreachable",
                                        structured_headers::Item::kTokenType);
        break;
      case SessionService::RefreshResult::kServerError:
        item = structured_headers::Item("server_error",
                                        structured_headers::Item::kTokenType);
        break;
      case SessionService::RefreshResult::kQuotaExceeded:
        item = structured_headers::Item("quota_exceeded",
                                        structured_headers::Item::kTokenType);
        break;
    }

    structured_headers::Parameters params = {
        {"session_identifier", structured_headers::Item(key.id.value())}};
    skipped_sessions_.emplace_back(std::move(item), std::move(params));
  }

  std::optional<std::string> Build() {
    if (skipped_sessions_.empty()) {
      return std::nullopt;
    }

    return structured_headers::SerializeList(std::move(skipped_sessions_));
  }

 private:
  structured_headers::List skipped_sessions_;
};

}  // namespace

DeferredURLRequest::DeferredURLRequest(
    const URLRequest* request,
    SessionService::RefreshCompleteCallback callback)
    : request(request), callback(std::move(callback)) {}

DeferredURLRequest::DeferredURLRequest(DeferredURLRequest&& other) noexcept =
    default;

DeferredURLRequest& DeferredURLRequest::operator=(
    DeferredURLRequest&& other) noexcept = default;

DeferredURLRequest::~DeferredURLRequest() = default;

SessionServiceImpl::SessionServiceImpl(
    unexportable_keys::UnexportableKeyService& key_service,
    const URLRequestContext* request_context,
    SessionStore* store)
    : pending_initialization_(!!store),
      key_service_(key_service),
      context_(request_context),
      session_store_(store) {
  ignore_refresh_quota_ =
      !base::FeatureList::IsEnabled(features::kDeviceBoundSessionsRefreshQuota);
  CHECK(context_);
}

SessionServiceImpl::~SessionServiceImpl() = default;

void SessionServiceImpl::LoadSessionsAsync() {
  if (!session_store_) {
    return;
  }
  session_store_->LoadSessions(base::BindOnce(
      &SessionServiceImpl::OnLoadSessionsComplete, weak_factory_.GetWeakPtr()));
}

void SessionServiceImpl::RegisterBoundSession(
    OnAccessCallback on_access_callback,
    RegistrationFetcherParam registration_params,
    const IsolationInfo& isolation_info,
    const NetLogWithSource& net_log,
    const std::optional<url::Origin>& original_request_initiator) {
  net::NetLogSource net_log_source_for_registration = net::NetLogSource(
      net::NetLogSourceType::URL_REQUEST, net::NetLog::Get()->NextID());
  net_log.AddEventReferencingSource(
      net::NetLogEventType::DBSC_REGISTRATION_REQUEST,
      net_log_source_for_registration);

  RegistrationFetcher::StartCreateTokenAndFetch(
      std::move(registration_params), key_service_.get(), context_.get(),
      isolation_info, net_log_source_for_registration,
      original_request_initiator,
      base::BindOnce(&SessionServiceImpl::OnRegistrationComplete,
                     weak_factory_.GetWeakPtr(),
                     std::move(on_access_callback)));
}

SessionServiceImpl::Observer::Observer(
    const GURL& url,
    base::RepeatingCallback<void(const SessionAccess&)> callback)
    : url(url), callback(callback) {}
SessionServiceImpl::Observer::~Observer() = default;

void SessionServiceImpl::OnLoadSessionsComplete(
    SessionStore::SessionsMap sessions) {
  unpartitioned_sessions_.merge(sessions);
  pending_initialization_ = false;

  std::vector<base::OnceClosure> queued_operations =
      std::move(queued_operations_);
  for (base::OnceClosure& closure : queued_operations) {
    std::move(closure).Run();
  }

  base::UmaHistogramCounts1000(
      "Net.DeviceBoundSessions.RequestsDeferredForInitialization",
      requests_before_initialization_);
}

void SessionServiceImpl::OnRegistrationComplete(
    OnAccessCallback on_access_callback,
    base::expected<SessionParams, SessionError> params_or_error) {
  SessionError::ErrorType result = OnRegistrationCompleteInternal(
      std::move(on_access_callback), std::move(params_or_error));
  base::UmaHistogramEnumeration("Net.DeviceBoundSessions.RegistrationResult",
                                result);
}

std::ranges::subrange<SessionServiceImpl::SessionsMap::iterator>
SessionServiceImpl::GetSessionsForSite(const SchemefulSite& site) {
  const auto now = base::Time::Now();
  // Session keys are sorted by site, then identifier. So the first
  // element not less than (`site`, "") is the first session for this
  // site.
  auto it =
      unpartitioned_sessions_.lower_bound(SessionKey{site, Session::Id("")});
  while (it != unpartitioned_sessions_.end() && it->first.site == site) {
    auto curit = it;
    ++it;

    if (now >= curit->second->expiry_date()) {
      // Since this deletion is not due to a request, we do not need to
      // provide a per-request callback here.
      DeleteSessionAndNotifyInternal(curit, base::NullCallback());
    } else {
      curit->second->RecordAccess();
    }
  }

  return std::ranges::subrange<SessionsMap::iterator>(
      unpartitioned_sessions_.lower_bound(SessionKey{site, Session::Id("")}),
      it);
}

std::optional<SessionService::DeferralParams> SessionServiceImpl::ShouldDefer(
    URLRequest* request,
    HttpRequestHeaders* extra_headers,
    const FirstPartySetMetadata& first_party_set_metadata) {
  if (pending_initialization_) {
    return DeferralParams();
  }
  SchemefulSite site(request->url());
  DebugHeaderBuilder debug_header_builder;
  const base::flat_map<SessionKey, RefreshResult>& previous_deferrals =
      request->device_bound_session_deferrals();
  for (const auto& [_, session] : GetSessionsForSite(site)) {
    if (session->ShouldDeferRequest(request, first_party_set_metadata)) {
      auto previous_deferrals_it =
          previous_deferrals.find({site, session->id()});
      if (previous_deferrals_it != previous_deferrals.end()) {
        debug_header_builder.AddSkippedSession(previous_deferrals_it->first,
                                               previous_deferrals_it->second);
        continue;
      }

      NotifySessionAccess(request->device_bound_session_access_callback(),
                          SessionAccess::AccessType::kUpdate, site, *session);
      return DeferralParams(session->id());
    }
  }

  std::optional<std::string> debug_header = debug_header_builder.Build();
  if (debug_header.has_value()) {
    extra_headers->SetHeader("Secure-Session-Skipped", *debug_header);
  }

  return std::nullopt;
}

void SessionServiceImpl::DeferRequestForRefresh(
    URLRequest* request,
    DeferralParams deferral,
    RefreshCompleteCallback callback) {
  CHECK(callback);
  CHECK(request);

  if (deferral.is_pending_initialization) {
    CHECK(pending_initialization_);
    requests_before_initialization_++;
    // Due to the need to recompute `first_party_set_metadata`, we always
    // restart the request after initialization completes.
    queued_operations_.push_back(base::BindOnce(
        std::move(callback), RefreshResult::kInitializedService));
    return;
  }

  Session::Id session_id = *deferral.session_id;
  // For the first deferring request, create a new vector and add the request.
  auto [it, inserted] = deferred_requests_.try_emplace(session_id);
  // Add the request to the deferred list.
  it->second.emplace_back(request, std::move(callback));

  SchemefulSite site(request->url());
  auto* session = GetSession(site, session_id);
  if (!session) {
    // If we can't find the session, clear the session_id key in the map
    // and continue all related requests. We can call this a fatal error
    // because the session has already been deleted.
    UnblockDeferredRequests(session_id, RefreshResult::kFatalError);
    return;
  }
  // Notify the request that it has been deferred for refreshed cookies.
  NotifySessionAccess(request->device_bound_session_access_callback(),
                      SessionAccess::AccessType::kUpdate, site, *session);
  if (!inserted) {
    return;
  }

  if (RefreshQuotaExceeded(site)) {
    UnblockDeferredRequests(session_id, RefreshResult::kQuotaExceeded);
    return;
  }

  if (session->ShouldBackoff()) {
    UnblockDeferredRequests(session_id, RefreshResult::kUnreachable);
    return;
  }

  const Session::KeyIdOrError& key_id = session->unexportable_key_id();
  if (!key_id.has_value()) {
    if (key_id.error() == unexportable_keys::ServiceError::kKeyNotReady) {
      // Unwrap key and then try to refresh
      session_store_->RestoreSessionBindingKey(
          site, session_id,
          base::BindOnce(&SessionServiceImpl::OnSessionKeyRestored,
                         weak_factory_.GetWeakPtr(), request, site, session_id,
                         request->device_bound_session_access_callback()));
    } else {
      UnblockDeferredRequests(session_id, RefreshResult::kFatalError);
      DeleteSessionAndNotify(site, session_id,
                             request->device_bound_session_access_callback());
    }

    return;
  }

  RefreshSessionInternal(request, site, session, *key_id);
}

void SessionServiceImpl::OnRefreshRequestCompletion(
    OnAccessCallback on_access_callback,
    SchemefulSite site,
    Session::Id session_id,
    base::expected<SessionParams, SessionError> params_or_error) {
  SessionError::ErrorType result = OnRefreshRequestCompletionInternal(
      std::move(on_access_callback), site, session_id,
      std::move(params_or_error));

  Session* session = GetSession(site, session_id);
  if (session) {
    session->InformOfRefreshResult(result);
  }

  base::UmaHistogramEnumeration("Net.DeviceBoundSessions.RefreshResult",
                                result);
}

// Continue or restart all deferred requests for the session and remove the
// session_id key in the map.
void SessionServiceImpl::UnblockDeferredRequests(const Session::Id& session_id,
                                                 RefreshResult result) {
  auto it = deferred_requests_.find(session_id);
  if (it == deferred_requests_.end()) {
    return;
  }

  auto requests = std::move(it->second);
  deferred_requests_.erase(it);

  for (auto& request : requests) {
    base::UmaHistogramTimes("Net.DeviceBoundSessions.RequestDeferredDuration",
                            request.timer.Elapsed());
    std::move(request.callback).Run(result);
  }
}

void SessionServiceImpl::SetChallengeForBoundSession(
    OnAccessCallback on_access_callback,
    const GURL& request_url,
    const SessionChallengeParam& param) {
  if (!param.session_id()) {
    return;
  }

  SchemefulSite site(request_url);
  for (const auto& [_, session] : GetSessionsForSite(site)) {
    if (session->id().value() == param.session_id()) {
      NotifySessionAccess(on_access_callback,
                          SessionAccess::AccessType::kUpdate, site, *session);
      session->set_cached_challenge(param.challenge());
      return;
    }
  }
}

void SessionServiceImpl::GetAllSessionsAsync(
    base::OnceCallback<void(const std::vector<SessionKey>&)> callback) {
  if (pending_initialization_) {
    queued_operations_.push_back(base::BindOnce(
        &SessionServiceImpl::GetAllSessionsAsync,
        // `base::Unretained` is safe because the callback is stored in
        // `queued_operations_`, which is owned by `this`.
        base::Unretained(this), std::move(callback)));
  } else {
    std::vector<SessionKey> sessions = base::ToVector(
        unpartitioned_sessions_, [](const auto& pair) { return pair.first; });
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::move(sessions)));
  }
}

void SessionServiceImpl::DeleteSessionAndNotify(
    const SchemefulSite& site,
    const Session::Id& id,
    SessionService::OnAccessCallback per_request_callback) {
  auto it = unpartitioned_sessions_.find(SessionKey{site, id});
  if (it == unpartitioned_sessions_.end()) {
    return;
  }

  DeleteSessionAndNotifyInternal(it, per_request_callback);
}

Session* SessionServiceImpl::GetSession(const SchemefulSite& site,
                                        const Session::Id& session_id) const {
  auto it = unpartitioned_sessions_.find(SessionKey{site, session_id});
  if (it != unpartitioned_sessions_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void SessionServiceImpl::AddSession(const SchemefulSite& site,
                                    std::unique_ptr<Session> session) {
  if (session_store_) {
    session_store_->SaveSession(site, *session);
  }

  unpartitioned_sessions_.emplace(SessionKey{site, session->id()},
                                  std::move(session));
}

void SessionServiceImpl::DeleteAllSessions(
    std::optional<base::Time> created_after_time,
    std::optional<base::Time> created_before_time,
    base::RepeatingCallback<bool(const url::Origin&, const net::SchemefulSite&)>
        origin_and_site_matcher,
    base::OnceClosure completion_callback) {
  for (auto it = unpartitioned_sessions_.begin();
       it != unpartitioned_sessions_.end();) {
    auto curit = it;
    ++it;

    if (SessionMatchesFilter(curit->first.site, *curit->second,
                             created_after_time, created_before_time,
                             origin_and_site_matcher)) {
      DeleteSessionAndNotifyInternal(curit, base::NullCallback());
    }
  }

  std::move(completion_callback).Run();
}

base::ScopedClosureRunner SessionServiceImpl::AddObserver(
    const GURL& url,
    base::RepeatingCallback<void(const SessionAccess&)> callback) {
  auto observer = std::make_unique<Observer>(url, callback);
  base::ScopedClosureRunner subscription(base::BindOnce(
      &SessionServiceImpl::RemoveObserver, weak_factory_.GetWeakPtr(),
      net::SchemefulSite(url), observer.get()));
  observers_by_site_[net::SchemefulSite(url)].insert(std::move(observer));
  return subscription;
}

void SessionServiceImpl::DeleteSessionAndNotifyInternal(
    SessionServiceImpl::SessionsMap::iterator it,
    SessionService::OnAccessCallback per_request_callback) {
  if (session_store_) {
    session_store_->DeleteSession(it->first);
  }

  NotifySessionAccess(per_request_callback,
                      SessionAccess::AccessType::kTermination, it->first.site,
                      *it->second);

  unpartitioned_sessions_.erase(it);
}

void SessionServiceImpl::NotifySessionAccess(
    SessionService::OnAccessCallback per_request_callback,
    SessionAccess::AccessType access_type,
    const SchemefulSite& site,
    const Session& session) {
  SessionAccess access{access_type, {site, session.id()}};

  if (access_type == SessionAccess::AccessType::kTermination) {
    access.cookies.reserve(session.cookies().size());
    for (const CookieCraving& cookie : session.cookies()) {
      access.cookies.push_back(cookie.Name());
    }
  }

  if (per_request_callback) {
    per_request_callback.Run(access);
  }

  auto observers_it = observers_by_site_.find(site);
  if (observers_it == observers_by_site_.end()) {
    return;
  }

  for (const auto& observer : observers_it->second) {
    if (session.IncludesUrl(observer->url)) {
      observer->callback.Run(access);
    }
  }
}

void SessionServiceImpl::RemoveObserver(net::SchemefulSite site,
                                        Observer* observer) {
  auto observers_it = observers_by_site_.find(site);
  if (observers_it == observers_by_site_.end()) {
    return;
  }

  ObserverSet& observers = observers_it->second;

  auto it = observers.find(observer);
  if (it == observers.end()) {
    return;
  }

  observers.erase(it);

  if (observers.empty()) {
    observers_by_site_.erase(observers_it);
  }
}

SessionError::ErrorType SessionServiceImpl::OnRegistrationCompleteInternal(
    OnAccessCallback on_access_callback,
    base::expected<SessionParams, SessionError> params_or_error) {
  if (!params_or_error.has_value()) {
    // There was a failure attempting to register. This registration
    // request could be used to clean up an existing session if the
    // server returned `"continue": false` in the JSON. In that case, we
    // need to delete the session. In all other cases, we failed to
    // create a new session, so there's nothing to clean up.
    const SessionError& error = params_or_error.error();
    if (error.type == SessionError::ErrorType::kServerRequestedTermination &&
        error.session_id.has_value()) {
      Session::Id session_id(*error.session_id);
      DeleteSessionAndNotify(error.site, session_id, on_access_callback);
    }
    return error.type;
  }

  const SessionParams& params = *params_or_error;
  const SchemefulSite site(url::Origin::Create(GURL(params.fetcher_url)));

  auto session_or_error = Session::CreateIfValid(std::move(params));
  if (!session_or_error.has_value()) {
    // The attempt to create a valid session failed. Since this specific
    // registration request did not add a session, no cleanup is needed.
    return session_or_error.error().type;
  }
  CHECK(*session_or_error);
  NotifySessionAccess(on_access_callback, SessionAccess::AccessType::kCreation,
                      site, **session_or_error);
  AddSession(site, std::move(*session_or_error));
  return SessionError::ErrorType::kSuccess;
}

SessionError::ErrorType SessionServiceImpl::OnRefreshRequestCompletionInternal(
    OnAccessCallback on_access_callback,
    const SchemefulSite& site,
    const Session::Id& session_id,
    base::expected<SessionParams, SessionError> params_or_error) {
  // If refresh succeeded:
  // 1. update the session by adding a new session and deleting the old one
  // 2. restart the deferred requests.
  //
  // Note that we notified `on_access_callback` about `session_id` already, so
  // we only need to notify the callback about other sessions.
  //
  // TODO(crbug.com/353766139): check if add/delete update will cause some race,
  // for example, if the the old session_id is still in use while deleting it.
  // Is it service's responsibility to keep the session_id same with the one in
  // received JSON which parsed as result_result->params?
  if (params_or_error.has_value()) {
    auto session_or_error = Session::CreateIfValid(*params_or_error);
    if (!session_or_error.has_value()) {
      // New parameters are invalid, terminate the session.
      DeleteSessionAndNotify(site, session_id, on_access_callback);
      UnblockDeferredRequests(session_id, RefreshResult::kFatalError);
      return session_or_error.error().type;
    }

    std::unique_ptr<Session> new_session = std::move(*session_or_error);
    CHECK(new_session);

    // Delete old session.
    DeleteSessionAndNotify(site, session_id,
                           new_session->id() == session_id
                               ? base::NullCallback()
                               : on_access_callback);
    // Add the new session.
    SchemefulSite new_site(
        url::Origin::Create(GURL(params_or_error->fetcher_url)));
    if (new_session->id() != session_id) {
      NotifySessionAccess(on_access_callback,
                          SessionAccess::AccessType::kCreation, new_site,
                          *new_session);
    }
    AddSession(new_site, std::move(new_session));
    // The session has been refreshed, restart the request.
    UnblockDeferredRequests(session_id, RefreshResult::kRefreshed);
  } else if (const SessionError& error = params_or_error.error();
             error.IsFatal()) {
    Session::Id session_to_terminate =
        error.session_id ? Session::Id(*error.session_id) : session_id;
    DeleteSessionAndNotify(error.site, session_to_terminate,
                           on_access_callback);
    UnblockDeferredRequests(session_id, RefreshResult::kFatalError);
  } else {
    // Transient error, unblock the request without cookies.
    UnblockDeferredRequests(session_id, error.IsServerError()
                                            ? RefreshResult::kServerError
                                            : RefreshResult::kUnreachable);
  }

  return params_or_error.has_value() ? SessionError::ErrorType::kSuccess
                                     : params_or_error.error().type;
}

void SessionServiceImpl::OnSessionKeyRestored(
    URLRequest* request,
    const SchemefulSite& site,
    const Session::Id& session_id,
    OnAccessCallback on_access_callback,
    Session::KeyIdOrError key_id_or_error) {
  if (!key_id_or_error.has_value()) {
    UnblockDeferredRequests(session_id, RefreshResult::kFatalError);
    DeleteSessionAndNotify(site, session_id, on_access_callback);
    return;
  }

  auto* session = GetSession(site, session_id);
  if (!session) {
    UnblockDeferredRequests(session_id, RefreshResult::kFatalError);
    return;
  }

  session->set_unexportable_key_id(key_id_or_error);

  RefreshSessionInternal(request, site, session, *key_id_or_error);
}

void SessionServiceImpl::RefreshSessionInternal(
    URLRequest* request,
    const SchemefulSite& site,
    Session* session,
    unexportable_keys::UnexportableKeyId key_id) {
  net::NetLogSource net_log_source_for_refresh = net::NetLogSource(
      net::NetLogSourceType::URL_REQUEST, net::NetLog::Get()->NextID());
  request->net_log().AddEventReferencingSource(
      net::NetLogEventType::DBSC_REFRESH_REQUEST, net_log_source_for_refresh);

  refresh_times_[site].push_back(base::TimeTicks::Now());

  auto callback = base::BindOnce(
      &SessionServiceImpl::OnRefreshRequestCompletion,
      weak_factory_.GetWeakPtr(),
      request->device_bound_session_access_callback(), site, session->id());
  RegistrationFetcher::StartFetchWithExistingKey(
      RegistrationRequestParam::CreateForRefresh(*session), key_service_.get(),
      context_.get(), request->isolation_info(), net_log_source_for_refresh,
      request->initiator(), std::move(callback), key_id);
}

bool SessionServiceImpl::RefreshQuotaExceeded(const SchemefulSite& site) {
  if (ignore_refresh_quota_) {
    return false;
  }

  auto it = refresh_times_.find(site);
  if (it == refresh_times_.end()) {
    return false;
  }

  it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
                                  [](base::TimeTicks time) {
                                    return base::TimeTicks::Now() - time >=
                                           kRefreshQuotaInterval;
                                  }),
                   it->second.end());

  size_t refresh_count = it->second.size();
  if (refresh_count == 0) {
    refresh_times_.erase(it);
  }

  return refresh_count >= kRefreshQuota;
}

}  // namespace net::device_bound_sessions
