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
  enum class SameSiteCookieMode {
    INCLUDE_STRICT_AND_LAX,
    INCLUDE_LAX,
    DO_NOT_INCLUDE
  };

  // Creates a CookieOptions object which:
  //
  // * Excludes HttpOnly cookies
  // * Excludes SameSite cookies
  // * Does not enforce prefix restrictions (e.g. "$Secure-*")
  // * Updates last-accessed time.
  //
  // These settings can be altered by calling:
  //
  // * |set_{include,exclude}_httponly()|
  // * |set_same_site_cookie_mode(
  //        CookieOptions::SameSiteCookieMode::INCLUDE_STRICT_AND_LAX)|
  // * |set_enforce_prefixes()|
  // * |set_do_not_update_access_time()|
  CookieOptions();

  void set_exclude_httponly() { exclude_httponly_ = true; }
  void set_include_httponly() { exclude_httponly_ = false; }
  bool exclude_httponly() const { return exclude_httponly_; }

  // Default is to exclude 'same_site' cookies.
  void set_same_site_cookie_mode(SameSiteCookieMode mode) {
    same_site_cookie_mode_ = mode;
  }
  SameSiteCookieMode same_site_cookie_mode() const {
    return same_site_cookie_mode_;
  }

  // |server_time| indicates what the server sending us the Cookie thought the
  // current time was when the cookie was produced.  This is used to adjust for
  // clock skew between server and host.
  void set_server_time(const base::Time& server_time) {
    server_time_ = server_time;
  }
  bool has_server_time() const { return !server_time_.is_null(); }
  base::Time server_time() const { return server_time_; }

  void set_update_access_time() { update_access_time_ = true; }
  void set_do_not_update_access_time() { update_access_time_ = false; }
  bool update_access_time() const { return update_access_time_; }

 private:
  bool exclude_httponly_;
  SameSiteCookieMode same_site_cookie_mode_;
  bool update_access_time_;
  base::Time server_time_;
};

}  // namespace net

#endif  // NET_COOKIES_COOKIE_OPTIONS_H_
