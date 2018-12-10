// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See "SSPI Sample Application" at
// http://msdn.microsoft.com/en-us/library/aa918273.aspx
// and "NTLM Security Support Provider" at
// http://msdn.microsoft.com/en-us/library/aa923611.aspx.

#include "net/http/http_auth_handler_ntlm.h"

#include "base/strings/string_util.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth_preferences.h"
#include "net/http/http_auth_sspi_win.h"

namespace net {

HttpAuthHandlerNTLM::HttpAuthHandlerNTLM(
    SSPILibrary* sspi_library,
    ULONG max_token_length,
    const HttpAuthPreferences* http_auth_preferences)
    : auth_sspi_(sspi_library, "NTLM", NTLMSP_NAME, max_token_length),
      http_auth_preferences_(http_auth_preferences) {}

HttpAuthHandlerNTLM::~HttpAuthHandlerNTLM() {
}

// Require identity on first pass instead of second.
bool HttpAuthHandlerNTLM::NeedsIdentity() {
  return auth_sspi_.NeedsIdentity();
}

bool HttpAuthHandlerNTLM::AllowsDefaultCredentials() {
  if (target_ == HttpAuth::AUTH_PROXY)
    return true;
  if (!http_auth_preferences_)
    return false;
  return http_auth_preferences_->CanUseDefaultCredentials(origin_);
}

HttpAuthHandlerNTLM::Factory::Factory()
    : max_token_length_(0),
      is_unsupported_(false) {
}

HttpAuthHandlerNTLM::Factory::~Factory() {
}

int HttpAuthHandlerNTLM::Factory::CreateAuthHandler(
    HttpAuthChallengeTokenizer* challenge,
    HttpAuth::Target target,
    const SSLInfo& ssl_info,
    const GURL& origin,
    CreateReason reason,
    int digest_nonce_count,
    const NetLogWithSource& net_log,
    std::unique_ptr<HttpAuthHandler>* handler) {
  if (is_unsupported_ || reason == CREATE_PREEMPTIVE)
    return ERR_UNSUPPORTED_AUTH_SCHEME;
  if (max_token_length_ == 0) {
    int rv = DetermineMaxTokenLength(sspi_library_.get(), NTLMSP_NAME,
                                     &max_token_length_);
    if (rv == ERR_UNSUPPORTED_AUTH_SCHEME)
      is_unsupported_ = true;
    if (rv != OK)
      return rv;
  }
  // TODO(cbentzel): Move towards model of parsing in the factory
  //                 method and only constructing when valid.
  std::unique_ptr<HttpAuthHandler> tmp_handler(new HttpAuthHandlerNTLM(
      sspi_library_.get(), max_token_length_, http_auth_preferences()));
  if (!tmp_handler->InitFromChallenge(challenge, target, ssl_info, origin,
                                      net_log))
    return ERR_INVALID_RESPONSE;
  handler->swap(tmp_handler);
  return OK;
}

}  // namespace net
