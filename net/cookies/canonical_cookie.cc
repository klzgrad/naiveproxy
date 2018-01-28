// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Portions of this code based on Mozilla:
//   (netwerk/cookie/src/nsCookieService.cpp)
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Witte (dwitte@stanford.edu)
 *   Michiel van Leeuwen (mvl@exedo.nl)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "net/cookies/canonical_cookie.h"

#include "base/format_macros.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "net/base/url_util.h"
#include "net/cookies/cookie_util.h"
#include "net/cookies/parsed_cookie.h"
#include "url/gurl.h"
#include "url/url_canon.h"
#include "url/url_util.h"

using base::Time;
using base::TimeDelta;

namespace net {

namespace {

const int kVlogSetCookies = 7;

// Determine the cookie domain to use for setting the specified cookie.
bool GetCookieDomain(const GURL& url,
                     const ParsedCookie& pc,
                     std::string* result) {
  std::string domain_string;
  if (pc.HasDomain())
    domain_string = pc.Domain();
  return cookie_util::GetCookieDomainWithString(url, domain_string, result);
}

// Compares cookies using name, domain and path, so that "equivalent" cookies
// (per RFC 2965) are equal to each other.
int PartialCookieOrdering(const CanonicalCookie& a, const CanonicalCookie& b) {
  int diff = a.Name().compare(b.Name());
  if (diff != 0)
    return diff;

  diff = a.Domain().compare(b.Domain());
  if (diff != 0)
    return diff;

  return a.Path().compare(b.Path());
}

}  // namespace

// Keep defaults here in sync with content/public/common/cookie_manager.mojom.
CanonicalCookie::CanonicalCookie()
    : secure_(false),
      httponly_(false),
      same_site_(CookieSameSite::NO_RESTRICTION),
      priority_(COOKIE_PRIORITY_MEDIUM) {}

CanonicalCookie::CanonicalCookie(const CanonicalCookie& other) = default;

CanonicalCookie::CanonicalCookie(const std::string& name,
                                 const std::string& value,
                                 const std::string& domain,
                                 const std::string& path,
                                 const base::Time& creation,
                                 const base::Time& expiration,
                                 const base::Time& last_access,
                                 bool secure,
                                 bool httponly,
                                 CookieSameSite same_site,
                                 CookiePriority priority)
    : name_(name),
      value_(value),
      domain_(domain),
      path_(path),
      creation_date_(creation),
      expiry_date_(expiration),
      last_access_date_(last_access),
      secure_(secure),
      httponly_(httponly),
      same_site_(same_site),
      priority_(priority) {}

CanonicalCookie::~CanonicalCookie() {}

// static
std::string CanonicalCookie::CanonPathWithString(
    const GURL& url,
    const std::string& path_string) {
  // The RFC says the path should be a prefix of the current URL path.
  // However, Mozilla allows you to set any path for compatibility with
  // broken websites.  We unfortunately will mimic this behavior.  We try
  // to be generous and accept cookies with an invalid path attribute, and
  // default the path to something reasonable.

  // The path was supplied in the cookie, we'll take it.
  if (!path_string.empty() && path_string[0] == '/')
    return path_string;

  // The path was not supplied in the cookie or invalid, we will default
  // to the current URL path.
  // """Defaults to the path of the request URL that generated the
  //    Set-Cookie response, up to, but not including, the
  //    right-most /."""
  // How would this work for a cookie on /?  We will include it then.
  const std::string& url_path = url.path();

  size_t idx = url_path.find_last_of('/');

  // The cookie path was invalid or a single '/'.
  if (idx == 0 || idx == std::string::npos)
    return std::string("/");

  // Return up to the rightmost '/'.
  return url_path.substr(0, idx);
}

// static
Time CanonicalCookie::CanonExpiration(const ParsedCookie& pc,
                                      const Time& current,
                                      const Time& server_time) {
  // First, try the Max-Age attribute.
  uint64_t max_age = 0;
  if (pc.HasMaxAge() &&
#ifdef COMPILER_MSVC
      sscanf_s(
#else
      sscanf(
#endif
             pc.MaxAge().c_str(), " %" PRIu64, &max_age) == 1) {
    return current + TimeDelta::FromSeconds(max_age);
  }

  // Try the Expires attribute.
  if (pc.HasExpires() && !pc.Expires().empty()) {
    // Adjust for clock skew between server and host.
    base::Time parsed_expiry =
        cookie_util::ParseCookieExpirationTime(pc.Expires());
    if (!parsed_expiry.is_null())
      return parsed_expiry + (current - server_time);
  }

  // Invalid or no expiration, persistent cookie.
  return Time();
}

// static
std::unique_ptr<CanonicalCookie> CanonicalCookie::Create(
    const GURL& url,
    const std::string& cookie_line,
    const base::Time& creation_time,
    const CookieOptions& options) {
  ParsedCookie parsed_cookie(cookie_line);

  if (!parsed_cookie.IsValid()) {
    VLOG(kVlogSetCookies) << "WARNING: Couldn't parse cookie";
    return nullptr;
  }

  if (options.exclude_httponly() && parsed_cookie.IsHttpOnly()) {
    VLOG(kVlogSetCookies) << "Create() is not creating a httponly cookie";
    return nullptr;
  }

  std::string cookie_domain;
  if (!GetCookieDomain(url, parsed_cookie, &cookie_domain)) {
    VLOG(kVlogSetCookies) << "Create() failed to get a cookie domain";
    return nullptr;
  }

  // Per 3.2.1 of "Deprecate modification of 'secure' cookies from non-secure
  // origins", if the cookie's "secure-only-flag" is "true" and the requesting
  // URL does not have a secure scheme, the cookie should be thrown away.
  // https://tools.ietf.org/html/draft-ietf-httpbis-cookie-alone
  if (parsed_cookie.IsSecure() && !url.SchemeIsCryptographic()) {
    VLOG(kVlogSetCookies)
        << "Create() is trying to create a secure cookie from an insecure URL";
    return nullptr;
  }

  std::string cookie_path = CanonPathWithString(
      url, parsed_cookie.HasPath() ? parsed_cookie.Path() : std::string());

  Time server_time(creation_time);
  if (options.has_server_time())
    server_time = options.server_time();

  DCHECK(!creation_time.is_null());
  Time cookie_expires = CanonicalCookie::CanonExpiration(parsed_cookie,
                                                         creation_time,
                                                         server_time);

  CookiePrefix prefix = GetCookiePrefix(parsed_cookie.Name());
  bool is_cookie_valid = IsCookiePrefixValid(prefix, url, parsed_cookie);
  RecordCookiePrefixMetrics(prefix, is_cookie_valid);
  if (!is_cookie_valid) {
    VLOG(kVlogSetCookies)
        << "Create() failed because the cookie violated prefix rules.";
    return nullptr;
  }

  std::unique_ptr<CanonicalCookie> cc(std::make_unique<CanonicalCookie>(
      parsed_cookie.Name(), parsed_cookie.Value(), cookie_domain, cookie_path,
      creation_time, cookie_expires, creation_time, parsed_cookie.IsSecure(),
      parsed_cookie.IsHttpOnly(), parsed_cookie.SameSite(),
      parsed_cookie.Priority()));
  DCHECK(cc->IsCanonical());
  return cc;
}

bool CanonicalCookie::IsEquivalentForSecureCookieMatching(
    const CanonicalCookie& ecc) const {
  return (name_ == ecc.Name() && (ecc.IsDomainMatch(DomainWithoutDot()) ||
                                  IsDomainMatch(ecc.DomainWithoutDot())) &&
          ecc.IsOnPath(Path()));
}

bool CanonicalCookie::IsOnPath(const std::string& url_path) const {

  // A zero length would be unsafe for our trailing '/' checks, and
  // would also make no sense for our prefix match.  The code that
  // creates a CanonicalCookie should make sure the path is never zero length,
  // but we double check anyway.
  if (path_.empty())
    return false;

  // The Mozilla code broke this into three cases, based on if the cookie path
  // was longer, the same length, or shorter than the length of the url path.
  // I think the approach below is simpler.

  // Make sure the cookie path is a prefix of the url path.  If the url path is
  // shorter than the cookie path, then the cookie path can't be a prefix.
  if (!base::StartsWith(url_path, path_, base::CompareCase::SENSITIVE))
    return false;

  // |url_path| is >= |path_|, and |path_| is a prefix of |url_path|.  If they
  // are the are the same length then they are identical, otherwise need an
  // additional check:

  // In order to avoid in correctly matching a cookie path of /blah
  // with a request path of '/blahblah/', we need to make sure that either
  // the cookie path ends in a trailing '/', or that we prefix up to a '/'
  // in the url path.  Since we know that the url path length is greater
  // than the cookie path length, it's safe to index one byte past.
  if (path_.length() != url_path.length() && path_.back() != '/' &&
      url_path[path_.length()] != '/') {
    return false;
  }

  return true;
}

bool CanonicalCookie::IsDomainMatch(const std::string& host) const {
  // Can domain match in two ways; as a domain cookie (where the cookie
  // domain begins with ".") or as a host cookie (where it doesn't).

  // Some consumers of the CookieMonster expect to set cookies on
  // URLs like http://.strange.url.  To retrieve cookies in this instance,
  // we allow matching as a host cookie even when the domain_ starts with
  // a period.
  if (host == domain_)
    return true;

  // Domain cookie must have an initial ".".  To match, it must be
  // equal to url's host with initial period removed, or a suffix of
  // it.

  // Arguably this should only apply to "http" or "https" cookies, but
  // extension cookie tests currently use the funtionality, and if we
  // ever decide to implement that it should be done by preventing
  // such cookies from being set.
  if (domain_.empty() || domain_[0] != '.')
    return false;

  // The host with a "." prefixed.
  if (domain_.compare(1, std::string::npos, host) == 0)
    return true;

  // A pure suffix of the host (ok since we know the domain already
  // starts with a ".")
  return (host.length() > domain_.length() &&
          host.compare(host.length() - domain_.length(),
                       domain_.length(), domain_) == 0);
}

bool CanonicalCookie::IncludeForRequestURL(const GURL& url,
                                           const CookieOptions& options) const {
  // Filter out HttpOnly cookies, per options.
  if (options.exclude_httponly() && IsHttpOnly())
    return false;
  // Secure cookies should not be included in requests for URLs with an
  // insecure scheme.
  if (IsSecure() && !url.SchemeIsCryptographic())
    return false;
  // Don't include cookies for requests that don't apply to the cookie domain.
  if (!IsDomainMatch(url.host()))
    return false;
  // Don't include cookies for requests with a url path that does not path
  // match the cookie-path.
  if (!IsOnPath(url.path()))
    return false;
  // Don't include same-site cookies for cross-site requests.
  switch (SameSite()) {
    case CookieSameSite::STRICT_MODE:
      if (options.same_site_cookie_mode() !=
          CookieOptions::SameSiteCookieMode::INCLUDE_STRICT_AND_LAX) {
        return false;
      }
      break;
    case CookieSameSite::LAX_MODE:
      if (options.same_site_cookie_mode() ==
          CookieOptions::SameSiteCookieMode::DO_NOT_INCLUDE) {
        return false;
      }
      break;
    default:
      break;
  }

  return true;
}

std::string CanonicalCookie::DebugString() const {
  return base::StringPrintf(
      "name: %s value: %s domain: %s path: %s creation: %" PRId64,
      name_.c_str(), value_.c_str(), domain_.c_str(), path_.c_str(),
      static_cast<int64_t>(creation_date_.ToTimeT()));
}

bool CanonicalCookie::PartialCompare(const CanonicalCookie& other) const {
  return PartialCookieOrdering(*this, other) < 0;
}

bool CanonicalCookie::FullCompare(const CanonicalCookie& other) const {
  // Do the partial comparison first.
  int diff = PartialCookieOrdering(*this, other);
  if (diff != 0)
    return diff < 0;

  DCHECK(IsEquivalent(other));

  // Compare other fields.
  diff = Value().compare(other.Value());
  if (diff != 0)
    return diff < 0;

  if (CreationDate() != other.CreationDate())
    return CreationDate() < other.CreationDate();

  if (ExpiryDate() != other.ExpiryDate())
    return ExpiryDate() < other.ExpiryDate();

  if (LastAccessDate() != other.LastAccessDate())
    return LastAccessDate() < other.LastAccessDate();

  if (IsSecure() != other.IsSecure())
    return IsSecure();

  if (IsHttpOnly() != other.IsHttpOnly())
    return IsHttpOnly();

  return Priority() < other.Priority();
}

bool CanonicalCookie::IsCanonical() const {
  // Not checking domain or path against ParsedCookie as it may have
  // come purely from the URL.
  if (ParsedCookie::ParseTokenString(name_) != name_ ||
      ParsedCookie::ParseValueString(value_) != value_ ||
      !ParsedCookie::IsValidCookieAttributeValue(name_) ||
      !ParsedCookie::IsValidCookieAttributeValue(value_)) {
    return false;
  }

  if (!last_access_date_.is_null() && creation_date_.is_null())
    return false;

  url::CanonHostInfo canon_host_info;
  std::string canonical_domain(CanonicalizeHost(domain_, &canon_host_info));
  // TODO(rdsmith): This specifically allows for empty domains.  The spec
  // suggests this is invalid (if a domain attribute is empty, the cookie's
  // domain is set to the canonicalized request host; see
  // https://tools.ietf.org/html/rfc6265#section-5.3).  However, it is
  // needed for Chrome extension cookies.
  // See http://crbug.com/730633 for more information.
  if (canonical_domain != domain_)
    return false;

  if (path_.empty() || path_[0] != '/')
    return false;

  switch (GetCookiePrefix(name_)) {
    case COOKIE_PREFIX_HOST:
      if (!secure_ || path_ != "/" || domain_.empty() || domain_[0] == '.')
        return false;
      break;
    case COOKIE_PREFIX_SECURE:
      if (!secure_)
        return false;
      break;
    default:
      break;
  }

  return true;
}

// static
CanonicalCookie::CookiePrefix CanonicalCookie::GetCookiePrefix(
    const std::string& name) {
  const char kSecurePrefix[] = "__Secure-";
  const char kHostPrefix[] = "__Host-";
  if (base::StartsWith(name, kSecurePrefix, base::CompareCase::SENSITIVE))
    return CanonicalCookie::COOKIE_PREFIX_SECURE;
  if (base::StartsWith(name, kHostPrefix, base::CompareCase::SENSITIVE))
    return CanonicalCookie::COOKIE_PREFIX_HOST;
  return CanonicalCookie::COOKIE_PREFIX_NONE;
}

// static
void CanonicalCookie::RecordCookiePrefixMetrics(
    CanonicalCookie::CookiePrefix prefix,
    bool is_cookie_valid) {
  const char kCookiePrefixHistogram[] = "Cookie.CookiePrefix";
  const char kCookiePrefixBlockedHistogram[] = "Cookie.CookiePrefixBlocked";
  UMA_HISTOGRAM_ENUMERATION(kCookiePrefixHistogram, prefix,
                            CanonicalCookie::COOKIE_PREFIX_LAST);
  if (!is_cookie_valid) {
    UMA_HISTOGRAM_ENUMERATION(kCookiePrefixBlockedHistogram, prefix,
                              CanonicalCookie::COOKIE_PREFIX_LAST);
  }
}

// Returns true if the cookie does not violate any constraints imposed
// by the cookie name's prefix, as described in
// https://tools.ietf.org/html/draft-west-cookie-prefixes
//
// static
bool CanonicalCookie::IsCookiePrefixValid(CanonicalCookie::CookiePrefix prefix,
                                          const GURL& url,
                                          const ParsedCookie& parsed_cookie) {
  if (prefix == CanonicalCookie::COOKIE_PREFIX_SECURE)
    return parsed_cookie.IsSecure() && url.SchemeIsCryptographic();
  if (prefix == CanonicalCookie::COOKIE_PREFIX_HOST) {
    return parsed_cookie.IsSecure() && url.SchemeIsCryptographic() &&
           !parsed_cookie.HasDomain() && parsed_cookie.Path() == "/";
  }
  return true;
}

std::string CanonicalCookie::DomainWithoutDot() const {
  if (domain_.empty() || domain_[0] != '.')
    return domain_;
  return domain_.substr(1);
}

}  // namespace net
