// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/cookie_constants.h"

#include "base/logging.h"
#include "base/strings/string_util.h"

namespace net {

const base::TimeDelta kLaxAllowUnsafeMaxAge = base::TimeDelta::FromMinutes(2);

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
  DCHECK(IsValidSameSiteValue(same_site));
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
    default:
      NOTREACHED();
      return "INVALID";
  }
}

CookieSameSite StringToCookieSameSite(const std::string& same_site) {
  CookieSameSite samesite = CookieSameSite::UNSPECIFIED;
  if (base::EqualsCaseInsensitiveASCII(same_site, kSameSiteNone))
    samesite = CookieSameSite::NO_RESTRICTION;
  if (base::EqualsCaseInsensitiveASCII(same_site, kSameSiteLax))
    samesite = CookieSameSite::LAX_MODE;
  if (base::EqualsCaseInsensitiveASCII(same_site, kSameSiteStrict))
    samesite = CookieSameSite::STRICT_MODE;
  if (base::EqualsCaseInsensitiveASCII(same_site, kSameSiteExtended))
    samesite = CookieSameSite::EXTENDED_MODE;
  DCHECK(IsValidSameSiteValue(samesite));
  return samesite;
}

bool IsValidSameSiteValue(CookieSameSite value) {
  switch (value) {
    case CookieSameSite::UNSPECIFIED:
    case CookieSameSite::NO_RESTRICTION:
    case CookieSameSite::LAX_MODE:
    case CookieSameSite::STRICT_MODE:
    case CookieSameSite::EXTENDED_MODE:
      return true;
    case CookieSameSite::LAX_MODE_ALLOW_UNSAFE:
      return false;
  }
  NOTREACHED();
  return false;
}

bool IsValidEffectiveSameSiteValue(CookieSameSite value) {
  switch (value) {
    case CookieSameSite::NO_RESTRICTION:
    case CookieSameSite::LAX_MODE:
    case CookieSameSite::LAX_MODE_ALLOW_UNSAFE:
    case CookieSameSite::STRICT_MODE:
      return true;
    case CookieSameSite::UNSPECIFIED:
    case CookieSameSite::EXTENDED_MODE:
      return false;
  }
  NOTREACHED();
  return false;
}

}  // namespace net
