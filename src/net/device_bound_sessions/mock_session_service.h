// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DEVICE_BOUND_SESSIONS_MOCK_SESSION_SERVICE_H_
#define NET_DEVICE_BOUND_SESSIONS_MOCK_SESSION_SERVICE_H_

#include <string>
#include <utility>

#include "base/containers/span.h"
#include "net/device_bound_sessions/registration_fetcher_param.h"
#include "net/device_bound_sessions/session_challenge_param.h"
#include "net/device_bound_sessions/session_service.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"

namespace net::device_bound_sessions {

class SessionServiceMock : public SessionService {
 public:
  SessionServiceMock();
  ~SessionServiceMock() override;

  MOCK_METHOD(void,
              RegisterBoundSession,
              (OnAccessCallback on_access_callback,
               RegistrationFetcherParam registration_params,
               const IsolationInfo& isolation_info,
               const NetLogWithSource& net_log,
               const std::optional<url::Origin>& original_request_initiator),
              (override));
  MOCK_METHOD(std::optional<SessionService::DeferralParams>,
              ShouldDefer,
              (URLRequest * request,
               const FirstPartySetMetadata& first_party_set_metadata),
              (override));
  MOCK_METHOD(void,
              DeferRequestForRefresh,
              (URLRequest * request,
               DeferralParams deferral,
               RefreshCompleteCallback restart_callback,
               RefreshCompleteCallback continue_callback),
              (override));
  MOCK_METHOD(void,
              SetChallengeForBoundSession,
              (OnAccessCallback on_access_callback,
               const GURL& request_url,
               const SessionChallengeParam& challenge_param),
              (override));
  MOCK_METHOD(
      void,
      GetAllSessionsAsync,
      (base::OnceCallback<void(const std::vector<SessionKey>&)> callback),
      (override));
  MOCK_METHOD(void,
              DeleteSessionAndNotify,
              (const SchemefulSite& site,
               const Session::Id& id,
               SessionService::OnAccessCallback per_request_callback),
              (override));
  MOCK_METHOD(void,
              DeleteAllSessions,
              (std::optional<base::Time> created_after_time,
               std::optional<base::Time> created_before_time,
               base::RepeatingCallback<bool(const url::Origin&,
                                            const net::SchemefulSite&)>
                   origin_and_site_matcher,
               base::OnceClosure completion_callback),
              (override));
  MOCK_METHOD(base::ScopedClosureRunner,
              AddObserver,
              (const GURL& url,
               base::RepeatingCallback<void(const SessionAccess&)> callback),
              (override));
};

}  // namespace net::device_bound_sessions

#endif  // NET_DEVICE_BOUND_SESSIONS_MOCK_SESSION_SERVICE_H_
