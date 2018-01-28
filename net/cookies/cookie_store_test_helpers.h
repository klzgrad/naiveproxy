// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_COOKIES_COOKIE_STORE_TEST_HELPERS_H_
#define NET_COOKIES_COOKIE_STORE_TEST_HELPERS_H_

#include "net/cookies/cookie_monster.h"

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

class DelayedCookieMonster : public CookieStore {
 public:
  DelayedCookieMonster();

  ~DelayedCookieMonster() override;

  // Call the asynchronous CookieMonster function, expect it to immediately
  // invoke the internal callback.
  // Post a delayed task to invoke the original callback with the results.

  void SetCookieWithOptionsAsync(
      const GURL& url,
      const std::string& cookie_line,
      const CookieOptions& options,
      CookieMonster::SetCookiesCallback callback) override;

  void SetCanonicalCookieAsync(std::unique_ptr<CanonicalCookie> cookie,
                               bool secure_source,
                               bool modify_http_only,
                               SetCookiesCallback callback) override;

  void GetCookiesWithOptionsAsync(
      const GURL& url,
      const CookieOptions& options,
      CookieMonster::GetCookiesCallback callback) override;

  void GetCookieListWithOptionsAsync(const GURL& url,
                                     const CookieOptions& options,
                                     GetCookieListCallback callback) override;

  void GetAllCookiesAsync(GetCookieListCallback callback) override;

  virtual bool SetCookieWithOptions(const GURL& url,
                                    const std::string& cookie_line,
                                    const CookieOptions& options);

  virtual std::string GetCookiesWithOptions(const GURL& url,
                                            const CookieOptions& options);

  virtual void DeleteCookie(const GURL& url,
                            const std::string& cookie_name);

  void DeleteCookieAsync(const GURL& url,
                         const std::string& cookie_name,
                         base::OnceClosure callback) override;

  void DeleteCanonicalCookieAsync(const CanonicalCookie& cookie,
                                  DeleteCallback callback) override;

  void DeleteAllCreatedBetweenAsync(const base::Time& delete_begin,
                                    const base::Time& delete_end,
                                    DeleteCallback callback) override;

  void DeleteAllCreatedBetweenWithPredicateAsync(
      const base::Time& delete_begin,
      const base::Time& delete_end,
      const base::Callback<bool(const CanonicalCookie&)>& predicate,
      DeleteCallback callback) override;

  void DeleteSessionCookiesAsync(DeleteCallback) override;

  void FlushStore(base::OnceClosure callback) override;

  std::unique_ptr<CookieStore::CookieChangedSubscription> AddCallbackForCookie(
      const GURL& url,
      const std::string& name,
      const CookieChangedCallback& callback) override;

  std::unique_ptr<CookieStore::CookieChangedSubscription>
  AddCallbackForAllChanges(const CookieChangedCallback& callback) override;

  bool IsEphemeral() override;

 private:
  // Be called immediately from CookieMonster.

  void SetCookiesInternalCallback(bool result);

  void GetCookiesWithOptionsInternalCallback(const std::string& cookie);
  void GetCookieListWithOptionsInternalCallback(const CookieList& cookie);

  // Invoke the original callbacks.

  void InvokeSetCookiesCallback(CookieMonster::SetCookiesCallback callback);

  void InvokeGetCookieStringCallback(
      CookieMonster::GetCookiesCallback callback);

  void InvokeGetCookieListCallback(
      CookieMonster::GetCookieListCallback callback);

  friend class base::RefCountedThreadSafe<DelayedCookieMonster>;

  std::unique_ptr<CookieMonster> cookie_monster_;

  bool did_run_;
  bool result_;
  std::string cookie_;
  std::string cookie_line_;
  CookieList cookie_list_;
};

class CookieURLHelper {
 public:
  explicit CookieURLHelper(const std::string& url_string);

  const std::string& domain() const { return domain_and_registry_; }
  std::string host() const { return url_.host(); }
  const GURL& url() const { return url_; }
  const GURL AppendPath(const std::string& path) const;

  // Return a new string with the following substitutions:
  // 1. "%R" -> Domain registry (i.e. "com")
  // 2. "%D" -> Domain + registry (i.e. "google.com")
  std::string Format(const std::string& format_string) const;

 private:
  const GURL url_;
  const std::string registry_;
  const std::string domain_and_registry_;
};

}  // namespace net

#endif  // NET_COOKIES_COOKIE_STORE_TEST_HELPERS_H_
