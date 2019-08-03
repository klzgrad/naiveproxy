// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/cookie_constants.h"

#include "base/logging.h"
#include "base/strings/string_util.h"

namespace net {

namespace {

const char kPriorityLow[] = "low";
const char kPriorityMedium[] = "medium";
const char kPriorityHigh[] = "high";

const char kSameSiteLax[] = "lax";
const char kSameSiteStrict[] = "strict";
const char kSameSiteNone[] = "none";
const char kSameSiteExtended[] = "extended";
const char kSameSiteUnspecified[] = "unspecified";

}  // namespace

std::string CookiePriorityToString(CookiePriority priority) {
  switch(priority) {
    case COOKIE_PRIORITY_HIGH:
      return kPriorityHigh;
    case COOKIE_PRIORITY_MEDIUM:
      return kPriorityMedium;
    case COOKIE_PRIORITY_LOW:
      return kPriorityLow;
    default:
      NOTREACHED();
  }
  return std::string();
}

CookiePriority StringToCookiePriority(const std::string& priority) {
  std::string priority_comp = base::ToLowerASCII(priority);

  if (priority_comp == kPriorityHigh)
    return COOKIE_PRIORITY_HIGH;
  if (priority_comp == kPriorityMedium)
    return COOKIE_PRIORITY_MEDIUM;
  if (priority_comp == kPriorityLow)
    return COOKIE_PRIORITY_LOW;

  return COOKIE_PRIORITY_DEFAULT;
}

std::string CookieSameSiteToString(CookieSameSite same_site) {
  switch (same_site) {
    case CookieSameSite::LAX_MODE:
      return kSameSiteLax;
    case CookieSameSite::STRICT_MODE:
      return kSameSiteStrict;
    case CookieSameSite::NO_RESTRICTION:
      return kSameSiteNone;
    case CookieSameSite::EXTENDED_MODE:
      return kSameSiteExtended;
    case CookieSameSite::UNSPECIFIED:
      return kSameSiteUnspecified;
  }
  return "INVALID";
}

CookieSameSite StringToCookieSameSite(const std::string& same_site) {
  if (base::EqualsCaseInsensitiveASCII(same_site, kSameSiteNone))
    return CookieSameSite::NO_RESTRICTION;
  if (base::EqualsCaseInsensitiveASCII(same_site, kSameSiteLax))
    return CookieSameSite::LAX_MODE;
  if (base::EqualsCaseInsensitiveASCII(same_site, kSameSiteStrict))
    return CookieSameSite::STRICT_MODE;
  if (base::EqualsCaseInsensitiveASCII(same_site, kSameSiteExtended))
    return CookieSameSite::EXTENDED_MODE;
  return CookieSameSite::UNSPECIFIED;
}

}  // namespace net
