// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_COOKIES_COOKIE_CONSTANTS_H_
#define NET_COOKIES_COOKIE_CONSTANTS_H_

#include <string>

#include "base/time/time.h"
#include "net/base/net_export.h"

namespace net {

// The time threshold for considering a cookie "short-lived" for the purposes of
// allowing unsafe methods for unspecified-SameSite cookies defaulted into Lax.
NET_EXPORT extern const base::TimeDelta kLaxAllowUnsafeMaxAge;

enum CookiePriority {
  COOKIE_PRIORITY_LOW     = 0,
  COOKIE_PRIORITY_MEDIUM  = 1,
  COOKIE_PRIORITY_HIGH    = 2,
  COOKIE_PRIORITY_DEFAULT = COOKIE_PRIORITY_MEDIUM
};

// See https://tools.ietf.org/html/draft-ietf-httpbis-cookie-same-site-00
// and https://tools.ietf.org/html/draft-ietf-httpbis-rfc6265bis for
// information about same site cookie restrictions.
// Note: Some values are allowed for a cookie's SameSite field (what literally
// came in the Set-Cookie line), and some are allowed for the effective SameSite
// (the actual rules to be applied when deciding whether the cookie can be
// accessed). Some are only allowed for one but not the other.
// Note: Don't renumber, as these values are persisted to a database.
enum class CookieSameSite {
  UNSPECIFIED = -1,    // Allowed for SameSite only.
  NO_RESTRICTION = 0,  // Allowed for SameSite and effective SameSite.
  LAX_MODE = 1,        // Allowed for SameSite and effective SameSite.
  STRICT_MODE = 2,     // Allowed for SameSite and effective SameSite.
  EXTENDED_MODE = 3,   // (Not implemented) Allowed for SameSite only.
  // Same as LAX_MODE, except cookie is also sent if the HTTP method is unsafe.
  LAX_MODE_ALLOW_UNSAFE = 4,  // Allowed for effective SameSite only.
};

// What rules to apply when determining when whether access to a particular
// cookie is allowed.
// TODO(crbug.com/978172): Machinery to read the content setting and set the
// appropriate CookieAccessSemantics on the cookie (will be added as a new
// metadata field of CanonicalCookie).
enum class CookieAccessSemantics {
  // Has not been checked yet.
  UNKNOWN = -1,
  // Has been checked and the cookie should *not* be subject to legacy access
  // rules.
  NONLEGACY = 0,
  // Has been checked and the cookie should be subject to legacy access rules.
  LEGACY,
};

// Returns the Set-Cookie header priority token corresponding to |priority|.
//
// TODO(mkwst): Remove this once its callsites are refactored.
NET_EXPORT std::string CookiePriorityToString(CookiePriority priority);

// Converts the Set-Cookie header priority token |priority| to a CookiePriority.
// Defaults to COOKIE_PRIORITY_DEFAULT for empty or unrecognized strings.
NET_EXPORT CookiePriority StringToCookiePriority(const std::string& priority);

// Returns a string corresponding to the value of the |same_site| token.
// Intended only for debugging/logging.
NET_EXPORT std::string CookieSameSiteToString(CookieSameSite same_site);

// Converts the Set-Cookie header SameSite token |same_site| to a
// CookieSameSite. Defaults to CookieSameSite::UNSPECIFIED for empty or
// unrecognized strings.
NET_EXPORT CookieSameSite StringToCookieSameSite(const std::string& same_site);

NET_EXPORT bool IsValidSameSiteValue(CookieSameSite value);
NET_EXPORT bool IsValidEffectiveSameSiteValue(CookieSameSite value);

}  // namespace net

#endif  // NET_COOKIES_COOKIE_CONSTANTS_H_
