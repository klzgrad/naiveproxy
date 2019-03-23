// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_AUTH_PREFERENCES_H_
#define NET_HTTP_HTTP_AUTH_PREFERENCES_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "build/build_config.h"
#include "net/base/net_export.h"
#include "url/gurl.h"

namespace net {

class URLSecurityManager;

// Manage the preferences needed for authentication, and provide a cache of
// them accessible from the IO thread.
class NET_EXPORT HttpAuthPreferences {
 public:
  HttpAuthPreferences();
  virtual ~HttpAuthPreferences();

  virtual bool NegotiateDisableCnameLookup() const;
  virtual bool NegotiateEnablePort() const;
#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  virtual bool NtlmV2Enabled() const;
#endif
#if defined(OS_ANDROID)
  virtual std::string AuthAndroidNegotiateAccountType() const;
#endif
  virtual bool CanUseDefaultCredentials(const GURL& auth_origin) const;
  virtual bool CanDelegate(const GURL& auth_origin) const;

  void set_negotiate_disable_cname_lookup(bool negotiate_disable_cname_lookup) {
    negotiate_disable_cname_lookup_ = negotiate_disable_cname_lookup;
  }

  void set_negotiate_enable_port(bool negotiate_enable_port) {
    negotiate_enable_port_ = negotiate_enable_port;
  }

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  void set_ntlm_v2_enabled(bool ntlm_v2_enabled) {
    ntlm_v2_enabled_ = ntlm_v2_enabled;
  }
#endif

  void SetServerWhitelist(const std::string& server_whitelist);

  void SetDelegateWhitelist(const std::string& delegate_whitelist);

#if defined(OS_ANDROID)
  void set_auth_android_negotiate_account_type(
      const std::string& account_type) {
    auth_android_negotiate_account_type_ = account_type;
  }
#endif

 private:
  bool negotiate_disable_cname_lookup_ = false;
  bool negotiate_enable_port_ = false;

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  bool ntlm_v2_enabled_ = true;
#endif

#if defined(OS_ANDROID)
  std::string auth_android_negotiate_account_type_;
#endif

  std::unique_ptr<URLSecurityManager> security_manager_;
  DISALLOW_COPY_AND_ASSIGN(HttpAuthPreferences);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_AUTH_PREFERENCES_H_
