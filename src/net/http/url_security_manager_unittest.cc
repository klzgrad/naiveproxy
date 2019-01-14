// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/url_security_manager.h"

#include <utility>

#include "net/base/net_errors.h"
#include "net/http/http_auth_filter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace net {

namespace {

struct TestData {
  const char* const url;
  bool succeds_in_windows_default;
  bool succeeds_in_whitelist;
};

const char kTestAuthWhitelist[] = "*example.com,*foobar.com,baz";

// Under Windows the following will be allowed by default:
//    localhost
//    host names without a period.
// In Posix systems (or on Windows if a whitelist is specified explicitly),
// everything depends on the whitelist.
const TestData kTestDataList[] = {
  { "http://localhost", true, false },
  { "http://bat", true, false },
  { "http://www.example.com", false, true },
  { "http://example.com", false, true },
  { "http://foobar.com", false, true },
  { "http://boo.foobar.com", false, true },
  { "http://baz", true, true },
  { "http://www.exampl.com", false, false },
  { "http://example.org", false, false },
  { "http://foobar.net", false, false },
  { "http://boo.fubar.com", false, false },
};

}  // namespace

TEST(URLSecurityManager, UseDefaultCredentials) {
  std::unique_ptr<HttpAuthFilter> auth_filter(
      new HttpAuthFilterWhitelist(kTestAuthWhitelist));
  ASSERT_TRUE(auth_filter);
  // The URL security manager takes ownership of |auth_filter|.
  std::unique_ptr<URLSecurityManager> url_security_manager(
      URLSecurityManager::Create());
  url_security_manager->SetDefaultWhitelist(std::move(auth_filter));
  ASSERT_TRUE(url_security_manager.get());

  for (size_t i = 0; i < arraysize(kTestDataList); ++i) {
    GURL gurl(kTestDataList[i].url);
    bool can_use_default =
        url_security_manager->CanUseDefaultCredentials(gurl);

    EXPECT_EQ(kTestDataList[i].succeeds_in_whitelist, can_use_default)
        << " Run: " << i << " URL: '" << gurl << "'";
  }
}

TEST(URLSecurityManager, CanDelegate) {
  std::unique_ptr<HttpAuthFilter> auth_filter(
      new HttpAuthFilterWhitelist(kTestAuthWhitelist));
  ASSERT_TRUE(auth_filter);
  // The URL security manager takes ownership of |auth_filter|.
  std::unique_ptr<URLSecurityManager> url_security_manager(
      URLSecurityManager::Create());
  url_security_manager->SetDelegateWhitelist(std::move(auth_filter));
  ASSERT_TRUE(url_security_manager.get());

  for (size_t i = 0; i < arraysize(kTestDataList); ++i) {
    GURL gurl(kTestDataList[i].url);
    bool can_delegate = url_security_manager->CanDelegate(gurl);
    EXPECT_EQ(kTestDataList[i].succeeds_in_whitelist, can_delegate)
        << " Run: " << i << " URL: '" << gurl << "'";
  }
}

TEST(URLSecurityManager, CanDelegate_NoWhitelist) {
  // Nothing can delegate in this case.
  std::unique_ptr<URLSecurityManager> url_security_manager(
      URLSecurityManager::Create());
  ASSERT_TRUE(url_security_manager.get());

  for (size_t i = 0; i < arraysize(kTestDataList); ++i) {
    GURL gurl(kTestDataList[i].url);
    bool can_delegate = url_security_manager->CanDelegate(gurl);
    EXPECT_FALSE(can_delegate);
  }
}


}  // namespace net
