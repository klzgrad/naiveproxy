// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_COOKIES_CANONICAL_COOKIE_H_
#define NET_COOKIES_CANONICAL_COOKIE_H_

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/optional.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_options.h"

class GURL;

namespace net {

class ParsedCookie;

class NET_EXPORT CanonicalCookie {
 public:
  CanonicalCookie();
  CanonicalCookie(const CanonicalCookie& other);

  // This constructor does not validate or canonicalize their inputs;
  // the resulting CanonicalCookies should not be relied on to be canonical
  // unless the caller has done appropriate validation and canonicalization
  // themselves.
  CanonicalCookie(const std::string& name,
                  const std::string& value,
                  const std::string& domain,
                  const std::string& path,
                  const base::Time& creation,
                  const base::Time& expiration,
                  const base::Time& last_access,
                  bool secure,
                  bool httponly,
                  CookieSameSite same_site,
                  CookiePriority priority);

  ~CanonicalCookie();

  // Supports the default copy constructor.

  // This enum represents if a cookie was included or excluded, and if excluded
  // why.
  enum class CookieInclusionStatus {
    INCLUDE = 0,
    EXCLUDE_HTTP_ONLY,
    EXCLUDE_SECURE_ONLY,
    EXCLUDE_DOMAIN_MISMATCH,
    EXCLUDE_NOT_ON_PATH,
    EXCLUDE_SAMESITE_STRICT,
    EXCLUDE_SAMESITE_LAX,
    // TODO(crbug.com/953995): Implement EXTENDED_MODE which will use the
    // following value.
    EXCLUDE_SAMESITE_EXTENDED,
    // The following two are used for the SameSiteByDefaultCookies experiment,
    // where if the SameSite attribute is not specified, it will be treated as
    // SameSite=Lax by default.
    EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX,
    // This is used if SameSite=None is specified, but the cookie is not Secure.
    // TODO(chlily): Implement the above.
    EXCLUDE_SAMESITE_NONE_INSECURE,
    EXCLUDE_USER_PREFERENCES,

    // Statuses specific to setting cookies
    EXCLUDE_FAILURE_TO_STORE,
    EXCLUDE_NONCOOKIEABLE_SCHEME,
    EXCLUDE_OVERWRITE_SECURE,
    EXCLUDE_OVERWRITE_HTTP_ONLY,
    EXCLUDE_INVALID_DOMAIN,
    EXCLUDE_INVALID_PREFIX,

    // Please keep last
    EXCLUDE_UNKNOWN_ERROR
  };

  // Creates a new |CanonicalCookie| from the |cookie_line| and the
  // |creation_time|.  Canonicalizes and validates inputs. May return NULL if
  // an attribute value is invalid.  |creation_time| may not be null. Sets
  // optional |status| to the relevent CookieInclusionStatus if provided
  static std::unique_ptr<CanonicalCookie> Create(
      const GURL& url,
      const std::string& cookie_line,
      const base::Time& creation_time,
      const CookieOptions& options,
      CookieInclusionStatus* status = nullptr);

  // Create a canonical cookie based on sanitizing the passed inputs in the
  // context of the passed URL.  Returns a null unique pointer if the inputs
  // cannot be sanitized.  If a cookie is created, |cookie->IsCanonical()|
  // will be true.
  static std::unique_ptr<CanonicalCookie> CreateSanitizedCookie(
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
      CookiePriority priority);

  const std::string& Name() const { return name_; }
  const std::string& Value() const { return value_; }
  const std::string& Domain() const { return domain_; }
  const std::string& Path() const { return path_; }
  const base::Time& CreationDate() const { return creation_date_; }
  const base::Time& LastAccessDate() const { return last_access_date_; }
  bool IsPersistent() const { return !expiry_date_.is_null(); }
  const base::Time& ExpiryDate() const { return expiry_date_; }
  bool IsSecure() const { return secure_; }
  bool IsHttpOnly() const { return httponly_; }
  CookieSameSite SameSite() const { return same_site_; }
  CookiePriority Priority() const { return priority_; }
  bool IsDomainCookie() const {
    return !domain_.empty() && domain_[0] == '.'; }
  bool IsHostCookie() const { return !IsDomainCookie(); }

  bool IsExpired(const base::Time& current) const {
    return !expiry_date_.is_null() && current >= expiry_date_;
  }

  // Are the cookies considered equivalent in the eyes of RFC 2965.
  // The RFC says that name must match (case-sensitive), domain must
  // match (case insensitive), and path must match (case sensitive).
  // For the case insensitive domain compare, we rely on the domain
  // having been canonicalized (in
  // GetCookieDomainWithString->CanonicalizeHost).
  bool IsEquivalent(const CanonicalCookie& ecc) const {
    // It seems like it would make sense to take secure, httponly, and samesite
    // into account, but the RFC doesn't specify this.
    // NOTE: Keep this logic in-sync with TrimDuplicateCookiesForHost().
    return (name_ == ecc.Name() && domain_ == ecc.Domain()
            && path_ == ecc.Path());
  }

  // Returns a key such that two cookies with the same UniqueKey() are
  // guaranteed to be equivalent in the sense of IsEquivalent().
  std::tuple<std::string, std::string, std::string> UniqueKey() const {
    return std::make_tuple(name_, domain_, path_);
  }

  // Checks a looser set of equivalency rules than 'IsEquivalent()' in order
  // to support the stricter 'Secure' behaviors specified in
  // https://tools.ietf.org/html/draft-ietf-httpbis-cookie-alone#section-3
  //
  // Returns 'true' if this cookie's name matches |ecc|, and this cookie is
  // a domain-match for |ecc| (or vice versa), and |ecc|'s path is "on" this
  // cookie's path (as per 'IsOnPath()').
  //
  // Note that while the domain-match cuts both ways (e.g. 'example.com'
  // matches 'www.example.com' in either direction), the path-match is
  // unidirectional (e.g. '/login/en' matches '/login' and '/', but
  // '/login' and '/' do not match '/login/en').
  bool IsEquivalentForSecureCookieMatching(const CanonicalCookie& ecc) const;

  void SetLastAccessDate(const base::Time& date) {
    last_access_date_ = date;
  }
  void SetCreationDate(const base::Time& date) { creation_date_ = date; }

  // Returns true if the given |url_path| path-matches the cookie-path as
  // described in section 5.1.4 in RFC 6265.
  bool IsOnPath(const std::string& url_path) const;

  // Returns true if the cookie domain matches the given |host| as described in
  // section 5.1.3 of RFC 6265.
  bool IsDomainMatch(const std::string& host) const;

  // Returns the effective SameSite mode to apply to this cookie. Depends on the
  // value of the given SameSite attribute and whether the
  // SameSiteByDefaultCookies feature is enabled.
  // Note: If you are converting to a different representation of a cookie, you
  // probably want to use SameSite() instead of this method. Otherwise, if you
  // are considering using this method, consider whether you should use
  // IncludeForRequestURL() or IsSetPermittedInContext() instead of doing the
  // SameSite computation yourself.
  CookieSameSite GetEffectiveSameSite() const;

  // Returns if the cookie should be included (and if not, why) for the given
  // request |url| using the CookieInclusionStatus enum. HTTP only cookies can
  // be filter by using appropriate cookie |options|. PLEASE NOTE that this
  // method does not check whether a cookie is expired or not!
  CookieInclusionStatus IncludeForRequestURL(
      const GURL& url,
      const CookieOptions& options) const;

  // Returns if the cookie with given attributes can be set in context described
  // by |options|, and if no, describes why.
  // WARNING: this does not cover checking whether secure cookies are set in
  // a secure schema, since whether the schema is secure isn't part of
  // |options|.
  CookieInclusionStatus IsSetPermittedInContext(
      const CookieOptions& options) const;

  std::string DebugString() const;

  static std::string CanonPathWithString(const GURL& url,
                                         const std::string& path_string);

  // Returns a "null" time if expiration was unspecified or invalid.
  static base::Time CanonExpiration(const ParsedCookie& pc,
                                    const base::Time& current,
                                    const base::Time& server_time);

  // Cookie ordering methods.

  // Returns true if the cookie is less than |other|, considering only name,
  // domain and path. In particular, two equivalent cookies (see IsEquivalent())
  // are identical for PartialCompare().
  bool PartialCompare(const CanonicalCookie& other) const;

  // TODO(chlily): Remove this. There should not be multiple cookies for which
  // PartialCompare disagrees. This is only used in tests.
  // Returns true if the cookie is less than |other|, considering all fields.
  // FullCompare() is consistent with PartialCompare(): cookies sorted using
  // FullCompare() are also sorted with respect to PartialCompare().
  bool FullCompare(const CanonicalCookie& other) const;

  // Return whether this object is a valid CanonicalCookie().  Invalid
  // cookies may be constructed by the detailed constructor.
  // A cookie is considered canonical if-and-only-if:
  // * It can be created by CanonicalCookie::Create, or
  // * It is identical to a cookie created by CanonicalCookie::Create except
  //   that the creation time is null, or
  // * It can be derived from a cookie created by CanonicalCookie::Create by
  //   entry into and retrieval from a cookie store (specifically, this means
  //   by the setting of an creation time in place of a null creation time, and
  //   the setting of a last access time).
  // An additional requirement on a CanonicalCookie is that if the last
  // access time is non-null, the creation time must also be non-null and
  // greater than the last access time.
  bool IsCanonical() const;

  // Returns the cookie line (e.g. "cookie1=value1; cookie2=value2") represented
  // by |cookies|. The string is built in the same order as the given list.
  static std::string BuildCookieLine(
      const std::vector<CanonicalCookie>& cookies);

 private:
  FRIEND_TEST_ALL_PREFIXES(CanonicalCookieTest, TestPrefixHistograms);

  // The special cookie prefixes as defined in
  // https://tools.ietf.org/html/draft-west-cookie-prefixes
  //
  // This enum is being histogrammed; do not reorder or remove values.
  enum CookiePrefix {
    COOKIE_PREFIX_NONE = 0,
    COOKIE_PREFIX_SECURE,
    COOKIE_PREFIX_HOST,
    COOKIE_PREFIX_LAST
  };

  // Returns the CookiePrefix (or COOKIE_PREFIX_NONE if none) that
  // applies to the given cookie |name|.
  static CookiePrefix GetCookiePrefix(const std::string& name);
  // Records histograms to measure how often cookie prefixes appear in
  // the wild and how often they would be blocked.
  static void RecordCookiePrefixMetrics(CookiePrefix prefix,
                                        bool is_cookie_valid);
  // Returns true if a prefixed cookie does not violate any of the rules
  // for that cookie.
  static bool IsCookiePrefixValid(CookiePrefix prefix,
                                  const GURL& url,
                                  const ParsedCookie& parsed_cookie);

  // Returns the cookie's domain, with the leading dot removed, if present.
  std::string DomainWithoutDot() const;

  std::string name_;
  std::string value_;
  std::string domain_;
  std::string path_;
  base::Time creation_date_;
  base::Time expiry_date_;
  base::Time last_access_date_;
  bool secure_;
  bool httponly_;
  CookieSameSite same_site_;
  CookiePriority priority_;
};

// These enable us to pass along a list of excluded cookie with the reason they
// were excluded
struct CookieWithStatus {
  CanonicalCookie cookie;
  CanonicalCookie::CookieInclusionStatus status;
};

// Used to pass excluded cookie information when it's possible that the
// canonical cookie object may not be available.
struct NET_EXPORT CookieAndLineWithStatus {
  CookieAndLineWithStatus();
  CookieAndLineWithStatus(base::Optional<CanonicalCookie> cookie,
                          std::string cookie_string,
                          CanonicalCookie::CookieInclusionStatus status);
  CookieAndLineWithStatus(
      const CookieAndLineWithStatus& cookie_and_line_with_status);

  CookieAndLineWithStatus& operator=(
      const CookieAndLineWithStatus& cookie_and_line_with_status);

  CookieAndLineWithStatus(
      CookieAndLineWithStatus&& cookie_and_line_with_status);

  ~CookieAndLineWithStatus();

  base::Optional<CanonicalCookie> cookie;
  std::string cookie_string;
  CanonicalCookie::CookieInclusionStatus status;
};

typedef std::vector<CanonicalCookie> CookieList;

typedef std::vector<CookieWithStatus> CookieStatusList;

typedef std::vector<CookieAndLineWithStatus> CookieAndLineStatusList;

}  // namespace net

#endif  // NET_COOKIES_CANONICAL_COOKIE_H_
