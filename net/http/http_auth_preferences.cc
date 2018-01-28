// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_preferences.h"

#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "net/http/http_auth_filter.h"
#include "net/http/url_security_manager.h"

namespace net {

HttpAuthPreferences::HttpAuthPreferences(
    const std::vector<std::string>& auth_schemes
#if defined(OS_POSIX) && !defined(OS_ANDROID) && !defined(OS_CHROMEOS)
    ,
    const std::string& gssapi_library_name
#endif
#if defined(OS_CHROMEOS)
    ,
    bool allow_gssapi_library_load
#endif
    )
    : auth_schemes_(auth_schemes.begin(), auth_schemes.end()),
      negotiate_disable_cname_lookup_(false),
      negotiate_enable_port_(false),
#if defined(OS_POSIX) && !defined(OS_ANDROID) && !defined(OS_CHROMEOS)
      gssapi_library_name_(gssapi_library_name),
#endif
#if defined(OS_CHROMEOS)
      allow_gssapi_library_load_(allow_gssapi_library_load),
#endif
      security_manager_(URLSecurityManager::Create()) {
}

HttpAuthPreferences::~HttpAuthPreferences() {}

bool HttpAuthPreferences::IsSupportedScheme(const std::string& scheme) const {
  return auth_schemes_.count(scheme) == 1;
}

bool HttpAuthPreferences::NegotiateDisableCnameLookup() const {
  return negotiate_disable_cname_lookup_;
}

bool HttpAuthPreferences::NegotiateEnablePort() const {
  return negotiate_enable_port_;
}

#if defined(OS_ANDROID)
std::string HttpAuthPreferences::AuthAndroidNegotiateAccountType() const {
  return auth_android_negotiate_account_type_;
}
#endif
#if defined(OS_POSIX) && !defined(OS_ANDROID) && !defined(OS_CHROMEOS)
std::string HttpAuthPreferences::GssapiLibraryName() const {
  return gssapi_library_name_;
}
#endif
#if defined(OS_CHROMEOS)
bool HttpAuthPreferences::AllowGssapiLibraryLoad() const {
  return allow_gssapi_library_load_;
}
#endif

bool HttpAuthPreferences::CanUseDefaultCredentials(
    const GURL& auth_origin) const {
  return security_manager_->CanUseDefaultCredentials(auth_origin);
}

bool HttpAuthPreferences::CanDelegate(const GURL& auth_origin) const {
  return security_manager_->CanDelegate(auth_origin);
}

void HttpAuthPreferences::set_server_whitelist(
    const std::string& server_whitelist) {
  if (server_whitelist.empty()) {
    security_manager_->SetDefaultWhitelist(std::unique_ptr<HttpAuthFilter>());
  } else {
    security_manager_->SetDefaultWhitelist(std::unique_ptr<HttpAuthFilter>(
        new net::HttpAuthFilterWhitelist(server_whitelist)));
  }
}

void HttpAuthPreferences::set_delegate_whitelist(
    const std::string& delegate_whitelist) {
  if (delegate_whitelist.empty()) {
    security_manager_->SetDelegateWhitelist(std::unique_ptr<HttpAuthFilter>());
  } else {
    security_manager_->SetDelegateWhitelist(std::unique_ptr<HttpAuthFilter>(
        new net::HttpAuthFilterWhitelist(delegate_whitelist)));
  }
}

}  // namespace net
