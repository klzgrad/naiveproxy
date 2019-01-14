// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_preferences.h"

#include <utility>

#include "base/stl_util.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "net/http/http_auth_filter.h"
#include "net/http/url_security_manager.h"

namespace net {

HttpAuthPreferences::HttpAuthPreferences()
    : security_manager_(URLSecurityManager::Create()) {}

HttpAuthPreferences::~HttpAuthPreferences() = default;

bool HttpAuthPreferences::NegotiateDisableCnameLookup() const {
  return negotiate_disable_cname_lookup_;
}

bool HttpAuthPreferences::NegotiateEnablePort() const {
  return negotiate_enable_port_;
}

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
bool HttpAuthPreferences::NtlmV2Enabled() const {
  return ntlm_v2_enabled_;
}
#endif

#if defined(OS_ANDROID)
std::string HttpAuthPreferences::AuthAndroidNegotiateAccountType() const {
  return auth_android_negotiate_account_type_;
}
#endif

bool HttpAuthPreferences::CanUseDefaultCredentials(
    const GURL& auth_origin) const {
  return security_manager_->CanUseDefaultCredentials(auth_origin);
}

bool HttpAuthPreferences::CanDelegate(const GURL& auth_origin) const {
  return security_manager_->CanDelegate(auth_origin);
}

void HttpAuthPreferences::SetServerWhitelist(
    const std::string& server_whitelist) {
  std::unique_ptr<HttpAuthFilter> whitelist;
  if (!server_whitelist.empty())
    whitelist = std::make_unique<HttpAuthFilterWhitelist>(server_whitelist);
  security_manager_->SetDefaultWhitelist(std::move(whitelist));
}

void HttpAuthPreferences::SetDelegateWhitelist(
    const std::string& delegate_whitelist) {
  std::unique_ptr<HttpAuthFilter> whitelist;
  if (!delegate_whitelist.empty())
    whitelist = std::make_unique<HttpAuthFilterWhitelist>(delegate_whitelist);
  security_manager_->SetDelegateWhitelist(std::move(whitelist));
}

}  // namespace net
