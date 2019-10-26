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
  // Ordered from least to most trusted environment.
  enum class SameSiteCookieContext {
    CROSS_SITE,
    // Same rules as lax but the http method is unsafe.
    SAME_SITE_LAX_METHOD_UNSAFE,
    SAME_SITE_LAX,
    SAME_SITE_STRICT
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

}  // namespace net

#endif  // NET_COOKIES_COOKIE_OPTIONS_H_
