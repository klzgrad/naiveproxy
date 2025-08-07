// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_handler.h"

#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"

namespace net {

HttpAuthHandler::HttpAuthHandler() = default;

HttpAuthHandler::~HttpAuthHandler() = default;

bool HttpAuthHandler::InitFromChallenge(
    HttpAuthChallengeTokenizer* challenge,
    HttpAuth::Target target,
    const SSLInfo& ssl_info,
    const NetworkAnonymizationKey& network_anonymization_key,
    const url::SchemeHostPort& scheme_host_port,
    const NetLogWithSource& net_log) {
  scheme_host_port_ = scheme_host_port;
  target_ = target;
  score_ = -1;
  properties_ = -1;
  net_log_ = net_log;

  auth_challenge_ = challenge->challenge_text();
  net_log_.BeginEvent(NetLogEventType::AUTH_HANDLER_INIT);
  bool ok = Init(challenge, ssl_info, network_anonymization_key);
  net_log_.EndEvent(NetLogEventType::AUTH_HANDLER_INIT, [&]() {
    base::Value::Dict params;
    params.Set("succeeded", ok);
    params.Set("allows_default_credentials", AllowsDefaultCredentials());
    return params;
  });

  // Init() is expected to set the scheme, realm, score, and properties.  The
  // realm may be empty.
  DCHECK(!ok || score_ != -1);
  DCHECK(!ok || properties_ != -1);
  DCHECK(!ok || auth_scheme_ != HttpAuth::AUTH_SCHEME_MAX);

  return ok;
}

int HttpAuthHandler::GenerateAuthToken(const AuthCredentials* credentials,
                                       const HttpRequestInfo* request,
                                       CompletionOnceCallback callback,
                                       std::string* auth_token) {
  DCHECK(!callback.is_null());
  DCHECK(request);
  DCHECK(credentials != nullptr || AllowsDefaultCredentials());
  DCHECK(auth_token != nullptr);
  DCHECK(callback_.is_null());
  callback_ = std::move(callback);
  net_log_.BeginEvent(NetLogEventType::AUTH_GENERATE_TOKEN);
  int rv = GenerateAuthTokenImpl(
      credentials, request,
      base::BindOnce(&HttpAuthHandler::OnGenerateAuthTokenComplete,
                     base::Unretained(this)),
      auth_token);
  if (rv != ERR_IO_PENDING)
    FinishGenerateAuthToken(rv);
  return rv;
}

bool HttpAuthHandler::NeedsIdentity() {
  return true;
}

bool HttpAuthHandler::AllowsDefaultCredentials() {
  return false;
}

bool HttpAuthHandler::AllowsExplicitCredentials() {
  return true;
}

void HttpAuthHandler::OnGenerateAuthTokenComplete(int rv) {
  CompletionOnceCallback callback = std::move(callback_);
  FinishGenerateAuthToken(rv);
  DCHECK(!callback.is_null());
  std::move(callback).Run(rv);
}

void HttpAuthHandler::FinishGenerateAuthToken(int rv) {
  DCHECK_NE(rv, ERR_IO_PENDING);
  net_log_.EndEventWithNetErrorCode(NetLogEventType::AUTH_GENERATE_TOKEN, rv);
  callback_.Reset();
}

HttpAuth::AuthorizationResult HttpAuthHandler::HandleAnotherChallenge(
    HttpAuthChallengeTokenizer* challenge) {
  auto authorization_result = HandleAnotherChallengeImpl(challenge);
  net_log_.AddEvent(NetLogEventType::AUTH_HANDLE_CHALLENGE, [&] {
    return HttpAuth::NetLogAuthorizationResultParams("authorization_result",
                                                     authorization_result);
  });
  return authorization_result;
}

}  // namespace net
