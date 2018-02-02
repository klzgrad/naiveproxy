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
    : HttpAuthPreferences(std::vector<std::string>()) {}

#if defined(OS_POSIX) && !defined(OS_ANDROID)
HttpAuthPreferences::HttpAuthPreferences(
    const std::vector<std::string>& auth_schemes)
    : HttpAuthPreferences(auth_schemes,
#if defined(OS_CHROMEOS)
                          true
#else
                          std::string()
#endif  // defined(OS_CHROMEOS)
                          ) {
}
#endif  // defined(OS_POSIX) && !defined(OS_ANDROID)

HttpAuthPreferences::HttpAuthPreferences(
    const std::vector<std::string>& auth_schemes
#if defined(OS_CHROMEOS)
    ,
    bool allow_gssapi_library_load
#elif defined(OS_POSIX) && !defined(OS_ANDROID)
    ,
    const std::string& gssapi_library_name
#endif
    )
    : auth_schemes_(auth_schemes.begin(), auth_schemes.end()),
      negotiate_disable_cname_lookup_(false),
      negotiate_enable_port_(false),
#if defined(OS_POSIX)
      ntlm_v2_enabled_(false),
#endif
#if defined(OS_CHROMEOS)
      allow_gssapi_library_load_(allow_gssapi_library_load),
#elif defined(OS_POSIX) && !defined(OS_ANDROID)
      gssapi_library_name_(gssapi_library_name),
#endif
      security_manager_(URLSecurityManager::Create()) {
}

HttpAuthPreferences::~HttpAuthPreferences() = default;

bool HttpAuthPreferences::IsSupportedScheme(const std::string& scheme) const {
  return base::ContainsKey(auth_schemes_, scheme);
}

bool HttpAuthPreferences::NegotiateDisableCnameLookup() const {
  return negotiate_disable_cname_lookup_;
}

bool HttpAuthPreferences::NegotiateEnablePort() const {
  return negotiate_enable_port_;
}

#if defined(OS_POSIX)
bool HttpAuthPreferences::NtlmV2Enabled() const {
  return ntlm_v2_enabled_;
}
#endif

#if defined(OS_ANDROID)
std::string HttpAuthPreferences::AuthAndroidNegotiateAccountType() const {
  return auth_android_negotiate_account_type_;
}
#elif defined(OS_CHROMEOS)
bool HttpAuthPreferences::AllowGssapiLibraryLoad() const {
  return allow_gssapi_library_load_;
}
#elif defined(OS_POSIX)
std::string HttpAuthPreferences::GssapiLibraryName() const {
  return gssapi_library_name_;
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
