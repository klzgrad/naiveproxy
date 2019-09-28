// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/cookie_store.h"

#include "base/bind.h"
#include "base/callback.h"
#include "net/cookies/cookie_options.h"

namespace net {

CookieStore::~CookieStore() = default;

void CookieStore::DeleteAllAsync(DeleteCallback callback) {
  DeleteAllCreatedInTimeRangeAsync(CookieDeletionInfo::TimeRange(),
                                   std::move(callback));
}

void CookieStore::SetForceKeepSessionState() {
  // By default, do nothing.
}

void CookieStore::GetAllCookiesForURLAsync(const GURL& url,
                                           GetCookieListCallback callback) {
  CookieOptions options;
  options.set_include_httponly();
  options.set_same_site_cookie_context(
      CookieOptions::SameSiteCookieContext::SAME_SITE_STRICT);
  options.set_do_not_update_access_time();
  GetCookieListWithOptionsAsync(url, options, std::move(callback));
}

void CookieStore::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_absolute_name) const {}

}  // namespace net
