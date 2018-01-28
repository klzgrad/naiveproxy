// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/string_util.h"
#include "net/http/http_auth_filter.h"
#include "url/gurl.h"

namespace net {

// Using a std::set<> has the benefit of removing duplicates automatically.
typedef std::set<base::string16> RegistryWhitelist;

// TODO(ahendrickson) -- Determine if we want separate whitelists for HTTP and
// HTTPS, one for both, or only an HTTP one.  My understanding is that the HTTPS
// entries in the registry mean that you are only allowed to connect to the site
// via HTTPS and still be considered 'safe'.

HttpAuthFilterWhitelist::HttpAuthFilterWhitelist(
    const std::string& server_whitelist) {
  SetWhitelist(server_whitelist);
}

HttpAuthFilterWhitelist::~HttpAuthFilterWhitelist() {
}

// Add a new domain |filter| to the whitelist, if it's not already there
bool HttpAuthFilterWhitelist::AddFilter(const std::string& filter,
                                        HttpAuth::Target target) {
  if ((target != HttpAuth::AUTH_SERVER) && (target != HttpAuth::AUTH_PROXY))
    return false;
  // All proxies pass
  if (target == HttpAuth::AUTH_PROXY)
    return true;
  rules_.AddRuleFromString(filter);
  return true;
}

void HttpAuthFilterWhitelist::AddRuleToBypassLocal() {
  rules_.AddRuleToBypassLocal();
}

bool HttpAuthFilterWhitelist::IsValid(const GURL& url,
                                      HttpAuth::Target target) const {
  if ((target != HttpAuth::AUTH_SERVER) && (target != HttpAuth::AUTH_PROXY))
    return false;
  // All proxies pass
  if (target == HttpAuth::AUTH_PROXY)
    return true;
  return rules_.Matches(url);
}

void HttpAuthFilterWhitelist::SetWhitelist(
    const std::string& server_whitelist) {
  rules_.ParseFromString(server_whitelist);
}

}  // namespace net
