// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_COOKIES_COOKIE_CONSTANTS_H_
#define NET_COOKIES_COOKIE_CONSTANTS_H_

#include <string>

#include "net/base/net_export.h"

namespace net {

enum CookiePriority {
  COOKIE_PRIORITY_LOW     = 0,
  COOKIE_PRIORITY_MEDIUM  = 1,
  COOKIE_PRIORITY_HIGH    = 2,
  COOKIE_PRIORITY_DEFAULT = COOKIE_PRIORITY_MEDIUM
};

// See https://tools.ietf.org/html/draft-ietf-httpbis-cookie-same-site-00
// and https://tools.ietf.org/html/draft-ietf-httpbis-rfc6265bis for
// information about same site cookie restrictions.
// Note: Don't renumber, as these values are persisted to a database.
enum class CookieSameSite {
  UNSPECIFIED = -1,
  NO_RESTRICTION = 0,
  LAX_MODE = 1,
  STRICT_MODE = 2,
  EXTENDED_MODE = 3
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

}  // namespace net

#endif  // NET_COOKIES_COOKIE_CONSTANTS_H_
