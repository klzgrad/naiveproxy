// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_COOKIES_COOKIE_ACCESS_RESULT_H_
#define NET_COOKIES_COOKIE_ACCESS_RESULT_H_

#include "net/base/net_export.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_inclusion_status.h"

namespace net {

struct NET_EXPORT CookieAccessResult {
  // Creating a CookieAccessResult with out any parameters will create a
  // CookieInclusionStatus that has no exclusion reasons, therefore
  // indicates inclusion.
  CookieAccessResult();
  CookieAccessResult(CookieEffectiveSameSite effective_same_site,
                     CookieInclusionStatus status,
                     CookieAccessSemantics access_semantics);

  explicit CookieAccessResult(CookieInclusionStatus status);

  CookieAccessResult(const CookieAccessResult& cookie_access_result);

  CookieAccessResult& operator=(const CookieAccessResult& cookie_access_result);

  CookieAccessResult(CookieAccessResult&& cookie_access_result);

  ~CookieAccessResult();

  CookieInclusionStatus status;
  CookieEffectiveSameSite effective_same_site =
      CookieEffectiveSameSite::UNDEFINED;
  CookieAccessSemantics access_semantics = CookieAccessSemantics::UNKNOWN;
};

}  // namespace net

#endif  // NET_COOKIES_COOKIE_ACCESS_RESULT_H_
