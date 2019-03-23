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

void CookieStore::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_absolute_name) const {}

CookieStore::CookieStore() : channel_id_service_id_(-1) {}

}  // namespace net
