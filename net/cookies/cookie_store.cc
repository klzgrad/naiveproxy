// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/cookie_store.h"

#include "base/bind.h"
#include "base/callback.h"
#include "net/cookies/cookie_options.h"

namespace net {

CookieStore::~CookieStore() = default;

bool CookieStore::ChangeCauseIsDeletion(CookieStore::ChangeCause cause) {
  return cause != CookieStore::ChangeCause::INSERTED;
}

// Keep in sync with CanonicalCookie::BuildCookieLine.
std::string CookieStore::BuildCookieLine(
    const std::vector<CanonicalCookie*>& cookies) {
  std::string cookie_line;
  for (auto* cookie : cookies) {
    if (!cookie_line.empty())
      cookie_line += "; ";
    // In Mozilla, if you set a cookie like "AAA", it will have an empty token
    // and a value of "AAA". When it sends the cookie back, it will send "AAA",
    // so we need to avoid sending "=AAA" for a blank token value.
    if (!cookie->Name().empty())
      cookie_line += cookie->Name() + "=";
    cookie_line += cookie->Value();
  }
  return cookie_line;
}

void CookieStore::DeleteAllAsync(DeleteCallback callback) {
  DeleteAllCreatedBetweenAsync(base::Time(), base::Time::Max(),
                               std::move(callback));
}

void CookieStore::SetForceKeepSessionState() {
  // By default, do nothing.
}

void CookieStore::GetAllCookiesForURLAsync(const GURL& url,
                                           GetCookieListCallback callback) {
  CookieOptions options;
  options.set_include_httponly();
  options.set_same_site_cookie_mode(
      CookieOptions::SameSiteCookieMode::INCLUDE_STRICT_AND_LAX);
  options.set_do_not_update_access_time();
  GetCookieListWithOptionsAsync(url, options, std::move(callback));
}

void CookieStore::SetChannelIDServiceID(int id) {
  DCHECK_EQ(-1, channel_id_service_id_);
  channel_id_service_id_ = id;
}

int CookieStore::GetChannelIDServiceID() {
  return channel_id_service_id_;
}

CookieStore::CookieStore() : channel_id_service_id_(-1) {}

}  // namespace net
