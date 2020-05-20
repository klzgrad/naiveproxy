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

#include <sstream>
#include <utility>

#include "base/feature_list.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "net/base/features.h"
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

void AppendCookieLineEntry(const CanonicalCookie& cookie,
                           std::string* cookie_line) {
  if (!cookie_line->empty())
    *cookie_line += "; ";
  // In Mozilla, if you set a cookie like "AAA", it will have an empty token
  // and a value of "AAA". When it sends the cookie back, it will send "AAA",
  // so we need to avoid sending "=AAA" for a blank token value.
  if (!cookie.Name().empty())
    *cookie_line += cookie.Name() + "=";
  *cookie_line += cookie.Value();
}

uint32_t GetExclusionBitmask(
    CanonicalCookie::CookieInclusionStatus::ExclusionReason reason) {
  return 1u << static_cast<uint32_t>(reason);
}

uint32_t GetWarningBitmask(
    CanonicalCookie::CookieInclusionStatus::WarningReason reason) {
  return 1u << static_cast<uint32_t>(reason);
}

void ApplySameSiteCookieWarningToStatus(
    CookieSameSite samesite,
    CookieEffectiveSameSite effective_samesite,
    bool is_secure,
    CookieOptions::SameSiteCookieContext same_site_context,
    CanonicalCookie::CookieInclusionStatus* status) {
  if (samesite == CookieSameSite::UNSPECIFIED &&
      same_site_context.context <
          CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_LAX) {
    status->AddWarningReason(CanonicalCookie::CookieInclusionStatus::
                                 WARN_SAMESITE_UNSPECIFIED_CROSS_SITE_CONTEXT);
  }
  if (effective_samesite == CookieEffectiveSameSite::LAX_MODE_ALLOW_UNSAFE &&
      same_site_context.context ==
          CookieOptions::SameSiteCookieContext::ContextType::
              SAME_SITE_LAX_METHOD_UNSAFE) {
    // This warning is more specific so remove the previous, more general,
    // warning.
    status->RemoveWarningReason(
        CanonicalCookie::CookieInclusionStatus::
            WARN_SAMESITE_UNSPECIFIED_CROSS_SITE_CONTEXT);
    status->AddWarningReason(CanonicalCookie::CookieInclusionStatus::
                                 WARN_SAMESITE_UNSPECIFIED_LAX_ALLOW_UNSAFE);
  }
  if (samesite == CookieSameSite::NO_RESTRICTION && !is_secure) {
    status->AddWarningReason(
        CanonicalCookie::CookieInclusionStatus::WARN_SAMESITE_NONE_INSECURE);
  }
  // If there are reasons to exclude the cookie other than the new SameSite
  // rules, don't warn about the cookie at all.
  status->MaybeClearSameSiteWarning();
}

// This function is used to indicate if the same-site context of a cookie should
// recorded for the histograms SameSiteDifferentSchemeRequest and
// SameSiteDifferentSchemeResponse. It returns true if the context is
// cross-scheme but not cross-site and there is an effective same-site. It
// should be removed when the histrograms are removed.
// TODO(https://crbug.com/1066231)
bool ShouldLogCrossSchemeForHistograms(
    const CookieOptions::SameSiteCookieContext& context,
    const CookieEffectiveSameSite effective_same_site) {
  bool correct_context =
      context.cross_schemeness !=
          CookieOptions::SameSiteCookieContext::CrossSchemeness::NONE &&
      context.context !=
          CookieOptions::SameSiteCookieContext::ContextType::CROSS_SITE;

  bool correct_effective_same_site =
      effective_same_site == CookieEffectiveSameSite::LAX_MODE ||
      effective_same_site == CookieEffectiveSameSite::STRICT_MODE ||
      effective_same_site == CookieEffectiveSameSite::LAX_MODE_ALLOW_UNSAFE;

  return correct_context && correct_effective_same_site;
}

}  // namespace

// Keep defaults here in sync with content/public/common/cookie_manager.mojom.
CanonicalCookie::CanonicalCookie()
    : secure_(false),
      httponly_(false),
      same_site_(CookieSameSite::NO_RESTRICTION),
      priority_(COOKIE_PRIORITY_MEDIUM),
      source_scheme_(CookieSourceScheme::kUnset) {}

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
                                 CookiePriority priority,
                                 CookieSourceScheme scheme_secure)
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
      priority_(priority),
      source_scheme_(scheme_secure) {}

CanonicalCookie::~CanonicalCookie() = default;

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

  // Invalid or no expiration, session cookie.
  return Time();
}

// static
std::unique_ptr<CanonicalCookie> CanonicalCookie::Create(
    const GURL& url,
    const std::string& cookie_line,
    const base::Time& creation_time,
    base::Optional<base::Time> server_time,
    CookieInclusionStatus* status) {
  // Put a pointer on the stack so the rest of the function can assign to it if
  // the default nullptr is passed in.
  CookieInclusionStatus blank_status;
  if (status == nullptr) {
    status = &blank_status;
  }
  *status = CookieInclusionStatus();

  ParsedCookie parsed_cookie(cookie_line);

  if (!parsed_cookie.IsValid()) {
    DVLOG(net::cookie_util::kVlogSetCookies)
        << "WARNING: Couldn't parse cookie";
    status->AddExclusionReason(CookieInclusionStatus::EXCLUDE_FAILURE_TO_STORE);
    // Don't continue, because an invalid ParsedCookie doesn't have any
    // attributes.
    // TODO(chlily): Log metrics.
    return nullptr;
  }

  std::string cookie_domain;
  if (!GetCookieDomain(url, parsed_cookie, &cookie_domain)) {
    DVLOG(net::cookie_util::kVlogSetCookies)
        << "Create() failed to get a valid cookie domain";
    status->AddExclusionReason(CookieInclusionStatus::EXCLUDE_INVALID_DOMAIN);
  }

  std::string cookie_path = CanonPathWithString(
      url, parsed_cookie.HasPath() ? parsed_cookie.Path() : std::string());

  Time cookie_server_time(creation_time);
  if (server_time.has_value() && !server_time->is_null())
    cookie_server_time = server_time.value();

  DCHECK(!creation_time.is_null());
  Time cookie_expires = CanonicalCookie::CanonExpiration(
      parsed_cookie, creation_time, cookie_server_time);

  CookiePrefix prefix = GetCookiePrefix(parsed_cookie.Name());
  bool is_cookie_prefix_valid = IsCookiePrefixValid(prefix, url, parsed_cookie);
  RecordCookiePrefixMetrics(prefix, is_cookie_prefix_valid);
  if (!is_cookie_prefix_valid) {
    DVLOG(net::cookie_util::kVlogSetCookies)
        << "Create() failed because the cookie violated prefix rules.";
    status->AddExclusionReason(CookieInclusionStatus::EXCLUDE_INVALID_PREFIX);
  }

  // TODO(chlily): Log metrics.
  if (!status->IsInclude())
    return nullptr;

  CookieSameSiteString samesite_string = CookieSameSiteString::kUnspecified;
  CookieSameSite samesite = parsed_cookie.SameSite(&samesite_string);
  RecordCookieSameSiteAttributeValueHistogram(samesite_string);
  CookieSourceScheme source_scheme = url.SchemeIsCryptographic()
                                         ? CookieSourceScheme::kSecure
                                         : CookieSourceScheme::kNonSecure;

  std::unique_ptr<CanonicalCookie> cc(std::make_unique<CanonicalCookie>(
      parsed_cookie.Name(), parsed_cookie.Value(), cookie_domain, cookie_path,
      creation_time, cookie_expires, creation_time, parsed_cookie.IsSecure(),
      parsed_cookie.IsHttpOnly(), samesite, parsed_cookie.Priority(),
      source_scheme));

  DCHECK(cc->IsCanonical());

  // TODO(chlily): Log metrics.
  return cc;
}

// static
// TODO(crbug.com/957184): This should ideally return a CookieInclusionStatus.
std::unique_ptr<CanonicalCookie> CanonicalCookie::CreateSanitizedCookie(
    const GURL& url,
    const std::string& name,
    const std::string& value,
    const std::string& domain,
    const std::string& path,
    base::Time creation_time,
    base::Time expiration_time,
    base::Time last_access_time,
    bool secure,
    bool http_only,
    CookieSameSite same_site,
    CookiePriority priority) {
  // Validate consistency of passed arguments.
  if (ParsedCookie::ParseTokenString(name) != name ||
      ParsedCookie::ParseValueString(value) != value ||
      !ParsedCookie::IsValidCookieAttributeValue(name) ||
      !ParsedCookie::IsValidCookieAttributeValue(value) ||
      ParsedCookie::ParseValueString(domain) != domain ||
      ParsedCookie::ParseValueString(path) != path) {
    return nullptr;
  }

  if (name.empty() && value.empty())
    return nullptr;

  // This validation step must happen before GetCookieDomainWithString, so it
  // doesn't fail DCHECKs.
  if (!cookie_util::DomainIsHostOnly(url.host()))
    return nullptr;

  std::string cookie_domain;
  if (!cookie_util::GetCookieDomainWithString(url, domain, &cookie_domain))
    return nullptr;

  CookieSourceScheme source_scheme = url.SchemeIsCryptographic()
                                         ? CookieSourceScheme::kSecure
                                         : CookieSourceScheme::kNonSecure;

  if (secure && source_scheme == CookieSourceScheme::kNonSecure)
    return nullptr;

  std::string cookie_path = CanonicalCookie::CanonPathWithString(url, path);
  if (!path.empty() && cookie_path != path)
    return nullptr;

  if (!IsCookiePrefixValid(GetCookiePrefix(name), url, secure, domain,
                           cookie_path)) {
    return nullptr;
  }

  if (!last_access_time.is_null() && creation_time.is_null())
    return nullptr;

  // Canonicalize path again to make sure it escapes characters as needed.
  url::Component path_component(0, cookie_path.length());
  url::RawCanonOutputT<char> canon_path;
  url::Component canon_path_component;
  url::CanonicalizePath(cookie_path.data(), path_component, &canon_path,
                        &canon_path_component);
  cookie_path = std::string(canon_path.data() + canon_path_component.begin,
                            canon_path_component.len);

  std::unique_ptr<CanonicalCookie> cc(std::make_unique<CanonicalCookie>(
      name, value, cookie_domain, cookie_path, creation_time, expiration_time,
      last_access_time, secure, http_only, same_site, priority, source_scheme));
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
  return cookie_util::IsDomainMatch(domain_, host);
}

CanonicalCookie::CookieInclusionStatus CanonicalCookie::IncludeForRequestURL(
    const GURL& url,
    const CookieOptions& options,
    CookieAccessSemantics access_semantics) const {
  base::TimeDelta cookie_age = base::Time::Now() - CreationDate();
  CookieInclusionStatus status;
  // Filter out HttpOnly cookies, per options.
  if (options.exclude_httponly() && IsHttpOnly())
    status.AddExclusionReason(CookieInclusionStatus::EXCLUDE_HTTP_ONLY);
  // Secure cookies should not be included in requests for URLs with an
  // insecure scheme.
  if (IsSecure() && !url.SchemeIsCryptographic())
    status.AddExclusionReason(CookieInclusionStatus::EXCLUDE_SECURE_ONLY);
  // Don't include cookies for requests that don't apply to the cookie domain.
  if (!IsDomainMatch(url.host()))
    status.AddExclusionReason(CookieInclusionStatus::EXCLUDE_DOMAIN_MISMATCH);
  // Don't include cookies for requests with a url path that does not path
  // match the cookie-path.
  if (!IsOnPath(url.path()))
    status.AddExclusionReason(CookieInclusionStatus::EXCLUDE_NOT_ON_PATH);
  // Don't include same-site cookies for cross-site requests.
  CookieEffectiveSameSite effective_same_site =
      GetEffectiveSameSite(access_semantics);
  // Log the effective SameSite mode that is applied to the cookie on this
  // request, if its SameSite was not specified.
  if (SameSite() == CookieSameSite::UNSPECIFIED) {
    UMA_HISTOGRAM_ENUMERATION("Cookie.SameSiteUnspecifiedEffective",
                              effective_same_site,
                              CookieEffectiveSameSite::COUNT);
  }
  UMA_HISTOGRAM_ENUMERATION(
      "Cookie.RequestSameSiteContext",
      options.same_site_cookie_context().context,
      CookieOptions::SameSiteCookieContext::ContextType::COUNT);

  switch (effective_same_site) {
    case CookieEffectiveSameSite::STRICT_MODE:
      if (options.same_site_cookie_context().context <
          CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_STRICT) {
        status.AddExclusionReason(
            CookieInclusionStatus::EXCLUDE_SAMESITE_STRICT);
      }
      break;
    case CookieEffectiveSameSite::LAX_MODE:
      if (options.same_site_cookie_context().context <
          CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_LAX) {
        // Log metrics for a cookie that would have been included under the
        // "Lax-allow-unsafe" intervention, had it been new enough.
        if (SameSite() == CookieSameSite::UNSPECIFIED &&
            options.same_site_cookie_context().context ==
                CookieOptions::SameSiteCookieContext::ContextType::
                    SAME_SITE_LAX_METHOD_UNSAFE) {
          UMA_HISTOGRAM_CUSTOM_TIMES(
              "Cookie.SameSiteUnspecifiedTooOldToAllowUnsafe", cookie_age,
              base::TimeDelta::FromMinutes(1), base::TimeDelta::FromDays(5),
              100);
        }
        status.AddExclusionReason(
            (SameSite() == CookieSameSite::UNSPECIFIED)
                ? CookieInclusionStatus::
                      EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX
                : CookieInclusionStatus::EXCLUDE_SAMESITE_LAX);
      }
      break;
    // TODO(crbug.com/990439): Add a browsertest for this behavior.
    case CookieEffectiveSameSite::LAX_MODE_ALLOW_UNSAFE:
      DCHECK(SameSite() == CookieSameSite::UNSPECIFIED);
      if (options.same_site_cookie_context().context <
          CookieOptions::SameSiteCookieContext::ContextType::
              SAME_SITE_LAX_METHOD_UNSAFE) {
        // TODO(chlily): Do we need a separate CookieInclusionStatus for this?
        status.AddExclusionReason(
            CookieInclusionStatus::EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX);
      } else if (options.same_site_cookie_context().context ==
                 CookieOptions::SameSiteCookieContext::ContextType::
                     SAME_SITE_LAX_METHOD_UNSAFE) {
        // Log metrics for cookies that activate the "Lax-allow-unsafe"
        // intervention. This histogram macro allows up to 3 minutes, which is
        // enough for the current threshold of 2 minutes.
        UMA_HISTOGRAM_MEDIUM_TIMES("Cookie.LaxAllowUnsafeCookieIncludedAge",
                                   cookie_age);
      }
      break;
    default:
      break;
  }

  // If both SameSiteByDefaultCookies and CookiesWithoutSameSiteMustBeSecure
  // are enabled, non-SameSite cookies without the Secure attribute should be
  // ignored. This can apply to cookies which were created before the
  // experimental options were enabled (as non-SameSite, insecure cookies cannot
  // be set while the options are on).
  if (access_semantics != CookieAccessSemantics::LEGACY &&
      cookie_util::IsCookiesWithoutSameSiteMustBeSecureEnabled() &&
      SameSite() == CookieSameSite::NO_RESTRICTION && !IsSecure()) {
    status.AddExclusionReason(
        CookieInclusionStatus::EXCLUDE_SAMESITE_NONE_INSECURE);
  }

  // TODO(chlily): Apply warning if SameSite-by-default is enabled but
  // access_semantics is LEGACY?
  ApplySameSiteCookieWarningToStatus(
      SameSite(), effective_same_site, IsSecure(),
      options.same_site_cookie_context(), &status);

  if (status.IsInclude()) {
    UMA_HISTOGRAM_ENUMERATION("Cookie.IncludedRequestEffectiveSameSite",
                              effective_same_site,
                              CookieEffectiveSameSite::COUNT);

    if (ShouldLogCrossSchemeForHistograms(options.same_site_cookie_context(),
                                          effective_same_site)) {
      // TODO(https://crbug.com/1066231)
      UMA_HISTOGRAM_ENUMERATION(
          "Cookie.SameSiteDifferentSchemeRequest",
          options.same_site_cookie_context().ConvertToMetricsValue(),
          CookieOptions::SameSiteCookieContext::MetricCount());
      AddSameSiteCrossSchemeWarning(&status,
                                    options.same_site_cookie_context());
    }
  }

  // TODO(chlily): Log metrics.
  return status;
}

CanonicalCookie::CookieInclusionStatus CanonicalCookie::IsSetPermittedInContext(
    const CookieOptions& options,
    CookieAccessSemantics access_semantics) const {
  CookieInclusionStatus status;
  IsSetPermittedInContext(options, access_semantics, &status);
  return status;
}

void CanonicalCookie::IsSetPermittedInContext(
    const CookieOptions& options,
    CookieAccessSemantics access_semantics,
    CookieInclusionStatus* status) const {
  if (options.exclude_httponly() && IsHttpOnly()) {
    DVLOG(net::cookie_util::kVlogSetCookies)
        << "HttpOnly cookie not permitted in script context.";
    status->AddExclusionReason(CookieInclusionStatus::EXCLUDE_HTTP_ONLY);
  }

  // If both SameSiteByDefaultCookies and CookiesWithoutSameSiteMustBeSecure
  // are enabled, non-SameSite cookies without the Secure attribute will be
  // rejected.
  if (access_semantics != CookieAccessSemantics::LEGACY &&
      cookie_util::IsCookiesWithoutSameSiteMustBeSecureEnabled() &&
      SameSite() == CookieSameSite::NO_RESTRICTION && !IsSecure()) {
    DVLOG(net::cookie_util::kVlogSetCookies)
        << "SetCookie() rejecting insecure cookie with SameSite=None.";
    status->AddExclusionReason(
        CanonicalCookie::CookieInclusionStatus::EXCLUDE_SAMESITE_NONE_INSECURE);
  }
  // Log whether a SameSite=None cookie is Secure or not.
  if (SameSite() == CookieSameSite::NO_RESTRICTION) {
    UMA_HISTOGRAM_BOOLEAN("Cookie.SameSiteNoneIsSecure", IsSecure());
  }

  CookieEffectiveSameSite effective_same_site =
      GetEffectiveSameSite(access_semantics);
  switch (effective_same_site) {
    case CookieEffectiveSameSite::STRICT_MODE:
      // This intentionally checks for `< SAME_SITE_LAX`, as we allow
      // `SameSite=Strict` cookies to be set for top-level navigations that
      // qualify for receipt of `SameSite=Lax` cookies.
      if (options.same_site_cookie_context().context <
          CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_LAX) {
        DVLOG(net::cookie_util::kVlogSetCookies)
            << "Trying to set a `SameSite=Strict` cookie from a "
               "cross-site URL.";
        status->AddExclusionReason(
            CookieInclusionStatus::EXCLUDE_SAMESITE_STRICT);
      }
      break;
    case CookieEffectiveSameSite::LAX_MODE:
    case CookieEffectiveSameSite::LAX_MODE_ALLOW_UNSAFE:
      if (options.same_site_cookie_context().context <
          CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_LAX) {
        if (SameSite() == CookieSameSite::UNSPECIFIED) {
          DVLOG(net::cookie_util::kVlogSetCookies)
              << "Cookies with no known SameSite attribute being treated as "
                 "lax; attempt to set from a cross-site URL denied.";
          status->AddExclusionReason(
              CookieInclusionStatus::
                  EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX);
        } else {
          DVLOG(net::cookie_util::kVlogSetCookies)
              << "Trying to set a `SameSite=Lax` cookie from a cross-site URL.";
          status->AddExclusionReason(
              CookieInclusionStatus::EXCLUDE_SAMESITE_LAX);
        }
      }
      break;
    default:
      break;
  }

  ApplySameSiteCookieWarningToStatus(
      SameSite(), effective_same_site, IsSecure(),
      options.same_site_cookie_context(), status);

  if (status->IsInclude()) {
    UMA_HISTOGRAM_ENUMERATION("Cookie.IncludedResponseEffectiveSameSite",
                              effective_same_site,
                              CookieEffectiveSameSite::COUNT);

    if (ShouldLogCrossSchemeForHistograms(options.same_site_cookie_context(),
                                          effective_same_site)) {
      // TODO(crbug.com/1034014): Change enum to one with less confusing
      // phrasing.
      // TODO(https://crbug.com/1066231)
      UMA_HISTOGRAM_ENUMERATION(
          "Cookie.SameSiteDifferentSchemeResponse",
          options.same_site_cookie_context().ConvertToMetricsValue(),
          CookieOptions::SameSiteCookieContext::MetricCount());
      AddSameSiteCrossSchemeWarning(status, options.same_site_cookie_context());
    }
  }

  // TODO(chlily): Log metrics.
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

bool CanonicalCookie::IsEffectivelySameSiteNone(
    CookieAccessSemantics access_semantics) const {
  return GetEffectiveSameSite(access_semantics) ==
         CookieEffectiveSameSite::NO_RESTRICTION;
}

CookieEffectiveSameSite CanonicalCookie::GetEffectiveSameSiteForTesting(
    CookieAccessSemantics access_semantics) const {
  return GetEffectiveSameSite(access_semantics);
}

// static
std::string CanonicalCookie::BuildCookieLine(const CookieList& cookies) {
  std::string cookie_line;
  for (const auto& cookie : cookies) {
    AppendCookieLineEntry(cookie, &cookie_line);
  }
  return cookie_line;
}

// static
std::string CanonicalCookie::BuildCookieLine(
    const CookieStatusList& cookie_status_list) {
  std::string cookie_line;
  for (const auto& cookie_with_status : cookie_status_list) {
    const CanonicalCookie& cookie = cookie_with_status.cookie;
    AppendCookieLineEntry(cookie, &cookie_line);
  }
  return cookie_line;
}

void net::CanonicalCookie::AddSameSiteCrossSchemeWarning(
    CookieInclusionStatus* status,
    CookieOptions::SameSiteCookieContext same_site_context) const {
  if (same_site_context.cross_schemeness ==
      CookieOptions::SameSiteCookieContext::CrossSchemeness::INSECURE_SECURE) {
    switch (same_site_context.context) {
      case CookieOptions::SameSiteCookieContext::ContextType::
          SAME_SITE_LAX_METHOD_UNSAFE:
        status->AddWarningReason(
            CookieInclusionStatus::
                WARN_SAMESITE_LAX_METHOD_UNSAFE_CROSS_SCHEME_SECURE_URL);
        break;
      case CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_LAX:
        status->AddWarningReason(
            CookieInclusionStatus::WARN_SAMESITE_LAX_CROSS_SCHEME_SECURE_URL);
        break;
      case CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_STRICT:
        status->AddWarningReason(
            CookieInclusionStatus::
                WARN_SAMESITE_STRICT_CROSS_SCHEME_SECURE_URL);
        break;
      default:
        break;
    }
  } else if (same_site_context.cross_schemeness ==
             CookieOptions::SameSiteCookieContext::CrossSchemeness::
                 SECURE_INSECURE) {
    switch (same_site_context.context) {
      case CookieOptions::SameSiteCookieContext::ContextType::
          SAME_SITE_LAX_METHOD_UNSAFE:
        status->AddWarningReason(
            CookieInclusionStatus::
                WARN_SAMESITE_LAX_METHOD_UNSAFE_CROSS_SCHEME_INSECURE_URL);
        break;
      case CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_LAX:
        status->AddWarningReason(
            CookieInclusionStatus::WARN_SAMESITE_LAX_CROSS_SCHEME_INSECURE_URL);
        break;
      case CookieOptions::SameSiteCookieContext::ContextType::SAME_SITE_STRICT:
        status->AddWarningReason(
            CookieInclusionStatus::
                WARN_SAMESITE_STRICT_CROSS_SCHEME_INSECURE_URL);
        break;
      default:
        break;
    }
  }
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
  return CanonicalCookie::IsCookiePrefixValid(
      prefix, url, parsed_cookie.IsSecure(),
      parsed_cookie.HasDomain() ? parsed_cookie.Domain() : "",
      parsed_cookie.HasPath() ? parsed_cookie.Path() : "");
}

bool CanonicalCookie::IsCookiePrefixValid(CanonicalCookie::CookiePrefix prefix,
                                          const GURL& url,
                                          bool secure,
                                          const std::string& domain,
                                          const std::string& path) {
  if (prefix == CanonicalCookie::COOKIE_PREFIX_SECURE)
    return secure && url.SchemeIsCryptographic();
  if (prefix == CanonicalCookie::COOKIE_PREFIX_HOST) {
    const bool domain_valid =
        domain.empty() || (url.HostIsIPAddress() && url.host() == domain);
    return secure && url.SchemeIsCryptographic() && domain_valid && path == "/";
  }
  return true;
}

CookieEffectiveSameSite CanonicalCookie::GetEffectiveSameSite(
    CookieAccessSemantics access_semantics) const {
  base::TimeDelta lax_allow_unsafe_threshold_age =
      base::FeatureList::IsEnabled(
          features::kSameSiteDefaultChecksMethodRigorously)
          ? base::TimeDelta::Min()
          : (base::FeatureList::IsEnabled(
                 features::kShortLaxAllowUnsafeThreshold)
                 ? kShortLaxAllowUnsafeMaxAge
                 : kLaxAllowUnsafeMaxAge);

  bool should_apply_same_site_lax_by_default =
      cookie_util::IsSameSiteByDefaultCookiesEnabled();
  if (access_semantics == CookieAccessSemantics::LEGACY) {
    should_apply_same_site_lax_by_default = false;
  } else if (access_semantics == CookieAccessSemantics::NONLEGACY) {
    should_apply_same_site_lax_by_default = true;
  }

  switch (SameSite()) {
    // If a cookie does not have a SameSite attribute, the effective SameSite
    // mode depends on the SameSiteByDefaultCookies setting and whether the
    // cookie is recently-created.
    case CookieSameSite::UNSPECIFIED:
      return should_apply_same_site_lax_by_default
                 ? (IsRecentlyCreated(lax_allow_unsafe_threshold_age)
                        ? CookieEffectiveSameSite::LAX_MODE_ALLOW_UNSAFE
                        : CookieEffectiveSameSite::LAX_MODE)
                 : CookieEffectiveSameSite::NO_RESTRICTION;
    case CookieSameSite::NO_RESTRICTION:
      return CookieEffectiveSameSite::NO_RESTRICTION;
    case CookieSameSite::LAX_MODE:
      return CookieEffectiveSameSite::LAX_MODE;
    case CookieSameSite::STRICT_MODE:
      return CookieEffectiveSameSite::STRICT_MODE;
  }
}

bool CanonicalCookie::IsRecentlyCreated(base::TimeDelta age_threshold) const {
  return (base::Time::Now() - creation_date_) <= age_threshold;
}

std::string CanonicalCookie::DomainWithoutDot() const {
  if (domain_.empty() || domain_[0] != '.')
    return domain_;
  return domain_.substr(1);
}

CanonicalCookie::CookieInclusionStatus::CookieInclusionStatus()
    : exclusion_reasons_(0u), warning_reasons_(0u) {}

CanonicalCookie::CookieInclusionStatus::CookieInclusionStatus(
    ExclusionReason reason)
    : exclusion_reasons_(GetExclusionBitmask(reason)) {}

CanonicalCookie::CookieInclusionStatus::CookieInclusionStatus(
    ExclusionReason reason,
    WarningReason warning)
    : exclusion_reasons_(GetExclusionBitmask(reason)),
      warning_reasons_(GetWarningBitmask(warning)) {}

bool CanonicalCookie::CookieInclusionStatus::operator==(
    const CookieInclusionStatus& other) const {
  return exclusion_reasons_ == other.exclusion_reasons_ &&
         warning_reasons_ == other.warning_reasons_;
}

bool CanonicalCookie::CookieInclusionStatus::operator!=(
    const CookieInclusionStatus& other) const {
  return !operator==(other);
}

bool CanonicalCookie::CookieInclusionStatus::IsInclude() const {
  return exclusion_reasons_ == 0u;
}

bool CanonicalCookie::CookieInclusionStatus::HasExclusionReason(
    ExclusionReason reason) const {
  return exclusion_reasons_ & GetExclusionBitmask(reason);
}

void CanonicalCookie::CookieInclusionStatus::AddExclusionReason(
    ExclusionReason reason) {
  exclusion_reasons_ |= GetExclusionBitmask(reason);
  // If the cookie would be excluded for reasons other than the new SameSite
  // rules, don't bother warning about it.
  MaybeClearSameSiteWarning();
}

void CanonicalCookie::CookieInclusionStatus::RemoveExclusionReason(
    ExclusionReason reason) {
  exclusion_reasons_ &= ~(GetExclusionBitmask(reason));
}

void CanonicalCookie::CookieInclusionStatus::MaybeClearSameSiteWarning() {
  uint32_t samesite_reasons_mask =
      GetExclusionBitmask(EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX) |
      GetExclusionBitmask(EXCLUDE_SAMESITE_NONE_INSECURE);
  if (exclusion_reasons_ & ~samesite_reasons_mask) {
    RemoveWarningReason(CanonicalCookie::CookieInclusionStatus::
                            WARN_SAMESITE_UNSPECIFIED_CROSS_SITE_CONTEXT);
    RemoveWarningReason(
        CanonicalCookie::CookieInclusionStatus::WARN_SAMESITE_NONE_INSECURE);
    RemoveWarningReason(CanonicalCookie::CookieInclusionStatus::
                            WARN_SAMESITE_UNSPECIFIED_LAX_ALLOW_UNSAFE);
  }
}

bool CanonicalCookie::CookieInclusionStatus::ShouldWarn() const {
  return warning_reasons_ != 0u;
}

bool CanonicalCookie::CookieInclusionStatus::HasWarningReason(
    WarningReason reason) const {
  return warning_reasons_ & GetWarningBitmask(reason);
}

bool net::CanonicalCookie::CookieInclusionStatus::HasCrossSchemeWarning(
    CookieInclusionStatus::WarningReason* reason) const {
  if (!ShouldWarn())
    return false;

  const CookieInclusionStatus::WarningReason cross_scheme_warnings[] = {
      WARN_SAMESITE_LAX_METHOD_UNSAFE_CROSS_SCHEME_SECURE_URL,
      WARN_SAMESITE_LAX_CROSS_SCHEME_SECURE_URL,
      WARN_SAMESITE_STRICT_CROSS_SCHEME_SECURE_URL,
      WARN_SAMESITE_LAX_METHOD_UNSAFE_CROSS_SCHEME_INSECURE_URL,
      WARN_SAMESITE_LAX_CROSS_SCHEME_INSECURE_URL,
      WARN_SAMESITE_STRICT_CROSS_SCHEME_INSECURE_URL};
  for (const auto warning : cross_scheme_warnings) {
    if (HasWarningReason(warning)) {
      if (reason)
        *reason = warning;
      return true;
    }
  }

  return false;
}

void CanonicalCookie::CookieInclusionStatus::AddWarningReason(
    WarningReason reason) {
  warning_reasons_ |= GetWarningBitmask(reason);
}

void CanonicalCookie::CookieInclusionStatus::RemoveWarningReason(
    WarningReason reason) {
  warning_reasons_ &= ~(GetWarningBitmask(reason));
}

std::string CanonicalCookie::CookieInclusionStatus::GetDebugString() const {
  std::string out;

  // Inclusion/exclusion
  if (IsInclude())
    base::StrAppend(&out, {"INCLUDE, "});
  if (HasExclusionReason(EXCLUDE_UNKNOWN_ERROR))
    base::StrAppend(&out, {"EXCLUDE_UNKNOWN_ERROR, "});
  if (HasExclusionReason(EXCLUDE_HTTP_ONLY))
    base::StrAppend(&out, {"EXCLUDE_HTTP_ONLY, "});
  if (HasExclusionReason(EXCLUDE_SECURE_ONLY))
    base::StrAppend(&out, {"EXCLUDE_SECURE_ONLY, "});
  if (HasExclusionReason(EXCLUDE_DOMAIN_MISMATCH))
    base::StrAppend(&out, {"EXCLUDE_DOMAIN_MISMATCH, "});
  if (HasExclusionReason(EXCLUDE_NOT_ON_PATH))
    base::StrAppend(&out, {"EXCLUDE_NOT_ON_PATH, "});
  if (HasExclusionReason(EXCLUDE_SAMESITE_STRICT))
    base::StrAppend(&out, {"EXCLUDE_SAMESITE_STRICT, "});
  if (HasExclusionReason(EXCLUDE_SAMESITE_LAX))
    base::StrAppend(&out, {"EXCLUDE_SAMESITE_LAX, "});
  if (HasExclusionReason(EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX))
    base::StrAppend(&out, {"EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX, "});
  if (HasExclusionReason(EXCLUDE_SAMESITE_NONE_INSECURE))
    base::StrAppend(&out, {"EXCLUDE_SAMESITE_NONE_INSECURE, "});
  if (HasExclusionReason(EXCLUDE_USER_PREFERENCES))
    base::StrAppend(&out, {"EXCLUDE_USER_PREFERENCES, "});
  if (HasExclusionReason(EXCLUDE_FAILURE_TO_STORE))
    base::StrAppend(&out, {"EXCLUDE_FAILURE_TO_STORE, "});
  if (HasExclusionReason(EXCLUDE_NONCOOKIEABLE_SCHEME))
    base::StrAppend(&out, {"EXCLUDE_NONCOOKIEABLE_SCHEME, "});
  if (HasExclusionReason(EXCLUDE_OVERWRITE_SECURE))
    base::StrAppend(&out, {"EXCLUDE_OVERWRITE_SECURE, "});
  if (HasExclusionReason(EXCLUDE_OVERWRITE_HTTP_ONLY))
    base::StrAppend(&out, {"EXCLUDE_OVERWRITE_HTTP_ONLY, "});
  if (HasExclusionReason(EXCLUDE_INVALID_DOMAIN))
    base::StrAppend(&out, {"EXCLUDE_INVALID_DOMAIN, "});
  if (HasExclusionReason(EXCLUDE_INVALID_PREFIX))
    base::StrAppend(&out, {"EXCLUDE_INVALID_PREFIX, "});

  // Add warning
  if (!ShouldWarn()) {
    base::StrAppend(&out, {"DO_NOT_WARN"});
    return out;
  }

  if (HasWarningReason(WARN_SAMESITE_UNSPECIFIED_CROSS_SITE_CONTEXT))
    base::StrAppend(&out, {"WARN_SAMESITE_UNSPECIFIED_CROSS_SITE_CONTEXT, "});
  if (HasWarningReason(WARN_SAMESITE_NONE_INSECURE))
    base::StrAppend(&out, {"WARN_SAMESITE_NONE_INSECURE, "});
  if (HasWarningReason(WARN_SAMESITE_UNSPECIFIED_LAX_ALLOW_UNSAFE))
    base::StrAppend(&out, {"WARN_SAMESITE_UNSPECIFIED_LAX_ALLOW_UNSAFE, "});
  if (HasWarningReason(WARN_SAMESITE_LAX_METHOD_UNSAFE_CROSS_SCHEME_SECURE_URL))
    base::StrAppend(
        &out, {"WARN_SAMESITE_LAX_METHOD_UNSAFE_CROSS_SCHEME_SECURE_URL, "});
  if (HasWarningReason(WARN_SAMESITE_LAX_CROSS_SCHEME_SECURE_URL))
    base::StrAppend(&out, {"WARN_SAMESITE_LAX_CROSS_SCHEME_SECURE_URL, "});
  if (HasWarningReason(WARN_SAMESITE_STRICT_CROSS_SCHEME_SECURE_URL))
    base::StrAppend(&out, {"WARN_SAMESITE_STRICT_CROSS_SCHEME_SECURE_URL, "});
  if (HasWarningReason(
          WARN_SAMESITE_LAX_METHOD_UNSAFE_CROSS_SCHEME_INSECURE_URL))
    base::StrAppend(
        &out, {"WARN_SAMESITE_LAX_METHOD_UNSAFE_CROSS_SCHEME_INSECURE_URL, "});
  if (HasWarningReason(WARN_SAMESITE_LAX_CROSS_SCHEME_INSECURE_URL))
    base::StrAppend(&out, {"WARN_SAMESITE_LAX_CROSS_SCHEME_INSECURE_URL, "});
  if (HasWarningReason(WARN_SAMESITE_STRICT_CROSS_SCHEME_INSECURE_URL))
    base::StrAppend(&out, {"WARN_SAMESITE_STRICT_CROSS_SCHEME_INSECURE_URL, "});

  // Strip trailing comma and space.
  out.erase(out.end() - 2, out.end());

  return out;
}

bool CanonicalCookie::CookieInclusionStatus::IsValid() const {
  // Bit positions where there should not be any true bits.
  uint32_t exclusion_mask = ~0u << static_cast<int>(NUM_EXCLUSION_REASONS);
  uint32_t warning_mask = ~0u << static_cast<int>(NUM_WARNING_REASONS);
  return (exclusion_mask & exclusion_reasons_) == 0u &&
         (warning_mask & warning_reasons_) == 0u;
}

bool CanonicalCookie::CookieInclusionStatus::
    HasExactlyExclusionReasonsForTesting(
        std::vector<CanonicalCookie::CookieInclusionStatus::ExclusionReason>
            reasons) const {
  CookieInclusionStatus expected = MakeFromReasonsForTesting(reasons);
  return expected.exclusion_reasons_ == exclusion_reasons_;
}

bool CanonicalCookie::CookieInclusionStatus::HasExactlyWarningReasonsForTesting(
    std::vector<WarningReason> reasons) const {
  CookieInclusionStatus expected = MakeFromReasonsForTesting({}, reasons);
  return expected.warning_reasons_ == warning_reasons_;
}

// static
CanonicalCookie::CookieInclusionStatus
CanonicalCookie::CookieInclusionStatus::MakeFromReasonsForTesting(
    std::vector<ExclusionReason> reasons,
    std::vector<WarningReason> warnings) {
  CookieInclusionStatus status;
  for (ExclusionReason reason : reasons) {
    status.AddExclusionReason(reason);
  }
  for (WarningReason warning : warnings) {
    status.AddWarningReason(warning);
  }
  return status;
}

CookieAndLineWithStatus::CookieAndLineWithStatus() = default;

CookieAndLineWithStatus::CookieAndLineWithStatus(
    base::Optional<CanonicalCookie> cookie,
    std::string cookie_string,
    CanonicalCookie::CookieInclusionStatus status)
    : cookie(std::move(cookie)),
      cookie_string(std::move(cookie_string)),
      status(status) {}

CookieAndLineWithStatus::CookieAndLineWithStatus(
    const CookieAndLineWithStatus&) = default;

CookieAndLineWithStatus& CookieAndLineWithStatus::operator=(
    const CookieAndLineWithStatus& cookie_and_line_with_status) = default;

CookieAndLineWithStatus::CookieAndLineWithStatus(CookieAndLineWithStatus&&) =
    default;

CookieAndLineWithStatus::~CookieAndLineWithStatus() = default;

}  // namespace net
