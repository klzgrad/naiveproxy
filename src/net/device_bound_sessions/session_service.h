// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DEVICE_BOUND_SESSIONS_SESSION_SERVICE_H_
#define NET_DEVICE_BOUND_SESSIONS_SESSION_SERVICE_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "net/base/net_export.h"
#include "net/device_bound_sessions/registration_fetcher_param.h"
#include "net/device_bound_sessions/session.h"
#include "net/device_bound_sessions/session_access.h"
#include "net/device_bound_sessions/session_challenge_param.h"
#include "net/device_bound_sessions/session_key.h"
#include "net/log/net_log_with_source.h"

namespace net {
class IsolationInfo;
class URLRequest;
class URLRequestContext;
}

namespace net::device_bound_sessions {

// Main class for Device Bound Session Credentials (DBSC).
// Full information can be found at https://github.com/WICG/dbsc
class NET_EXPORT SessionService {
 public:
  using RefreshCompleteCallback = base::OnceClosure;
  using OnAccessCallback = base::RepeatingCallback<void(const SessionAccess&)>;

  // Returns nullptr if unexportable key provider is not supported by the
  // platform or the device.
  static std::unique_ptr<SessionService> Create(
      const URLRequestContext* request_context);

  SessionService(const SessionService&) = delete;
  SessionService& operator=(const SessionService&) = delete;

  virtual ~SessionService() = default;

  // Called to register a new session after getting a Sec-Session-Registration
  // header.
  // Registration parameters to be used for creating the registration
  // request.
  // Isolation info to be used for registration request, this should be the
  // same as was used for the response with the Sec-Session-Registration
  // header.
  // `net_log` is the log corresponding to the request receiving the
  // Sec-Session-Registration header.
  // 'original_request_initiator` was the initiator for the request that
  // received the Sec-Session-Registration header.
  virtual void RegisterBoundSession(
      OnAccessCallback on_access_callback,
      RegistrationFetcherParam registration_params,
      const IsolationInfo& isolation_info,
      const NetLogWithSource& net_log,
      const std::optional<url::Origin>& original_request_initiator) = 0;

  // Check if a request should be deferred due to the session cookie being
  // missing. This should only be called once the request has the correct
  // cookies added to the request.
  // If multiple sessions needs to be refreshed for this request,
  // any of them can be returned.
  // Returns the session id if the request should be deferred, returns
  // std::nullopt if the request does not need to be deferred.
  virtual std::optional<Session::Id> GetAnySessionRequiringDeferral(
      URLRequest* request) = 0;

  // Defer a request while refreshing the session.
  // session_id is the identifier of the session that is required to be
  // refreshed.
  // Provides two callbacks, will always call one of them:
  // - The first will query for cookies for the requests again.
  // - The second to send the request without query for cookies again.
  virtual void DeferRequestForRefresh(
      URLRequest* request,
      Session::Id session_id,
      RefreshCompleteCallback restart_callback,
      RefreshCompleteCallback continue_callback) = 0;

  // Set the challenge for a bound session after getting a
  // Sec-Session-Challenge header.
  virtual void SetChallengeForBoundSession(
      OnAccessCallback on_access_callback,
      const GURL& request_url,
      const SessionChallengeParam& param) = 0;

  // Get all sessions. If sessions have not yet been loaded from disk,
  // defer until completely initialized.
  virtual void GetAllSessionsAsync(
      base::OnceCallback<void(const std::vector<SessionKey>&)> callback) = 0;

  // Delete the session on `site` with `id`, notifying
  // `per_request_callback` about any deletions.
  virtual void DeleteSessionAndNotify(
      const SchemefulSite& site,
      const Session::Id& id,
      SessionService::OnAccessCallback per_request_callback) = 0;

  // Delete all sessions that match the filtering arguments. See
  // `device_bound_sessions.mojom` for details on the filtering logic.
  virtual void DeleteAllSessions(
      std::optional<base::Time> created_after_time,
      std::optional<base::Time> created_before_time,
      base::RepeatingCallback<bool(const net::SchemefulSite&)> site_matcher,
      base::OnceClosure completion_callback) = 0;

  // Add an observer for session changes that include `url`. `callback`
  // will only be notified until the destruction of the returned
  // `ScopedClosureRunner`.
  virtual base::ScopedClosureRunner AddObserver(
      const GURL& url,
      base::RepeatingCallback<void(const SessionAccess&)> callback) = 0;

 protected:
  SessionService() = default;
};

}  // namespace net::device_bound_sessions

#endif  // NET_DEVICE_BOUND_SESSIONS_SESSION_SERVICE_H_
