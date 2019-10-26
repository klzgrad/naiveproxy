// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/cookie_store.h"

#include "base/bind.h"
#include "base/callback.h"

namespace net {

CookieStore::~CookieStore() = default;

void CookieStore::DeleteAllAsync(DeleteCallback callback) {
  DeleteAllCreatedInTimeRangeAsync(CookieDeletionInfo::TimeRange(),
                                   std::move(callback));
}

void CookieStore::SetForceKeepSessionState() {
  // By default, do nothing.
}

void CookieStore::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_absolute_name) const {}

}  // namespace net
