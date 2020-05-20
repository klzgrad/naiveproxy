// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Brought to you by number 42.

#include "net/cookies/cookie_options.h"

namespace net {

CookieOptions::SameSiteCookieContext
CookieOptions::SameSiteCookieContext::MakeInclusive() {
  return SameSiteCookieContext(ContextType::SAME_SITE_STRICT);
}

int64_t CookieOptions::SameSiteCookieContext::ConvertToMetricsValue() const {
  if (cross_schemeness == CrossSchemeness::INSECURE_SECURE) {
    return static_cast<int64_t>(context) | kToSecureMask;
  } else if (cross_schemeness == CrossSchemeness::SECURE_INSECURE) {
    return static_cast<int64_t>(context) | kToInsecureMask;
  }
  return static_cast<int64_t>(context);
}

// Keep default values in sync with content/public/common/cookie_manager.mojom.
CookieOptions::CookieOptions()
    : exclude_httponly_(true),
      same_site_cookie_context_(SameSiteCookieContext(
          SameSiteCookieContext::ContextType::CROSS_SITE)),
      update_access_time_(true),
      return_excluded_cookies_(false) {}

// static
CookieOptions CookieOptions::MakeAllInclusive() {
  CookieOptions options;
  options.set_include_httponly();
  options.set_same_site_cookie_context(SameSiteCookieContext::MakeInclusive());
  options.set_do_not_update_access_time();
  return options;
}

bool operator==(const CookieOptions::SameSiteCookieContext& lhs,
                const CookieOptions::SameSiteCookieContext& rhs) {
  return std::tie(lhs.context, lhs.cross_schemeness) ==
         std::tie(rhs.context, rhs.cross_schemeness);
}

bool operator!=(const CookieOptions::SameSiteCookieContext& lhs,
                const CookieOptions::SameSiteCookieContext& rhs) {
  return !(lhs == rhs);
}

}  // namespace net
