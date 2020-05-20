// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Brought to you by number 42.

#ifndef NET_COOKIES_COOKIE_OPTIONS_H_
#define NET_COOKIES_COOKIE_OPTIONS_H_

#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/cookies/cookie_constants.h"
#include "url/gurl.h"

namespace net {

class NET_EXPORT CookieOptions {
 public:

  // Relation between the cookie and the navigational environment.
  class NET_EXPORT SameSiteCookieContext {
   public:
    // CROSS_SITE to SAME_SITE_STRICT are ordered from least to most trusted
    // environment. Don't renumber, used in histograms.
    enum class ContextType {
      CROSS_SITE = 0,
      // Same rules as lax but the http method is unsafe.
      SAME_SITE_LAX_METHOD_UNSAFE = 1,
      SAME_SITE_LAX = 2,
      SAME_SITE_STRICT = 3,

      // Keep last, used for histograms.
      COUNT
    };

    // Used for when, and in what direction, same-site requests and responses
    // are made in a cross-scheme context. Currently only used for metrics
    // gathering and does not affect cookie behavior.
    enum class CrossSchemeness {
      NONE,
      INSECURE_SECURE,  // Insecure site-for-cookies, secure request/response
      SECURE_INSECURE   // Secure site-for-cookies, insecure request/response
    };

    SameSiteCookieContext() : SameSiteCookieContext(ContextType::CROSS_SITE) {}
    explicit SameSiteCookieContext(
        ContextType same_site_context,
        CrossSchemeness cross_schemeness = CrossSchemeness::NONE)
        : context(same_site_context), cross_schemeness(cross_schemeness) {}

    // Convenience method which returns a SameSiteCookieContext with the most
    // inclusive context. This allows access to all SameSite cookies.
    static SameSiteCookieContext MakeInclusive();

    // The following functions are for conversion to the previous style of
    // SameSiteCookieContext for metrics usage. This may be removed when the
    // metrics using them are also removed.

    // Used as the "COUNT" entry in a histogram enum.
    static constexpr int64_t MetricCount() {
      return (static_cast<int>(ContextType::SAME_SITE_STRICT) |
              kToInsecureMask) +
             1;
    }
    int64_t ConvertToMetricsValue() const;

    ContextType context;

    CrossSchemeness cross_schemeness;

   private:
    // The following variables are for conversion to the previous style of
    // SameSiteCookieContext for metrics usage. This may be removed when the
    // metrics using them are also removed.
    // Mask indicating insecure site-for-cookies and secure request/response.
    static const int kToSecureMask = 1 << 5;
    // Mask indicating secure site-for-cookies and insecure request/response.
    static const int kToInsecureMask = kToSecureMask << 1;
  };

  // Creates a CookieOptions object which:
  //
  // * Excludes HttpOnly cookies
  // * Excludes SameSite cookies
  // * Updates last-accessed time.
  // * Does not report excluded cookies in APIs that can do so.
  //
  // These settings can be altered by calling:
  //
  // * |set_{include,exclude}_httponly()|
  // * |set_same_site_cookie_context(
  //        CookieOptions::SameSiteCookieContext::SAME_SITE_STRICT)|
  // * |set_do_not_update_access_time()|
  CookieOptions();

  void set_exclude_httponly() { exclude_httponly_ = true; }
  void set_include_httponly() { exclude_httponly_ = false; }
  bool exclude_httponly() const { return exclude_httponly_; }

  // How trusted is the current browser environment when it comes to accessing
  // SameSite cookies. Default is not trusted, e.g. CROSS_SITE.
  void set_same_site_cookie_context(SameSiteCookieContext context) {
    same_site_cookie_context_ = context;
  }

  // Strips off the cross-scheme bits to only return the same-site context.
  SameSiteCookieContext same_site_cookie_context() const {
    return same_site_cookie_context_;
  }

  void set_update_access_time() { update_access_time_ = true; }
  void set_do_not_update_access_time() { update_access_time_ = false; }
  bool update_access_time() const { return update_access_time_; }

  void set_return_excluded_cookies() { return_excluded_cookies_ = true; }
  void unset_return_excluded_cookies() { return_excluded_cookies_ = false; }
  bool return_excluded_cookies() const { return return_excluded_cookies_; }

  // Convenience method for where you need a CookieOptions that will
  // work for getting/setting all types of cookies, including HttpOnly and
  // SameSite cookies. Also specifies not to update the access time, because
  // usually this is done to get all the cookies to check that they are correct,
  // including the creation time. This basically makes a CookieOptions that is
  // the opposite of the default CookieOptions.
  static CookieOptions MakeAllInclusive();

 private:
  bool exclude_httponly_;
  SameSiteCookieContext same_site_cookie_context_;
  bool update_access_time_;
  bool return_excluded_cookies_;
};

NET_EXPORT bool operator==(const CookieOptions::SameSiteCookieContext& lhs,
                           const CookieOptions::SameSiteCookieContext& rhs);

NET_EXPORT bool operator!=(const CookieOptions::SameSiteCookieContext& lhs,
                           const CookieOptions::SameSiteCookieContext& rhs);

}  // namespace net

#endif  // NET_COOKIES_COOKIE_OPTIONS_H_
