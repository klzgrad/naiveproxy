// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/site_for_cookies.h"

#include <vector>

#include "base/strings/strcat.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_util.h"

namespace net {
namespace {

// Tests that all URLs from |equivalent| produce SiteForCookies that match
// URLs in the set and are equivalent to each other, and are distinct and
// don't match |distinct|.
void TestEquivalentAndDistinct(const std::vector<GURL>& equivalent,
                               const std::vector<GURL>& distinct,
                               const std::string& expect_host) {
  for (const GURL& equiv_url_a : equivalent) {
    SiteForCookies equiv_a = SiteForCookies::FromUrl(equiv_url_a);
    EXPECT_EQ(equiv_a.scheme(), equiv_url_a.scheme());

    EXPECT_EQ(equiv_a.RepresentativeUrl().spec(),
              base::StrCat({equiv_a.scheme(), "://", expect_host, "/"}));

    for (const GURL& equiv_url_b : equivalent) {
      SiteForCookies equiv_b = SiteForCookies::FromUrl(equiv_url_a);

      EXPECT_TRUE(equiv_a.IsEquivalent(equiv_b));
      EXPECT_TRUE(equiv_b.IsEquivalent(equiv_a));
      EXPECT_TRUE(equiv_a.IsFirstParty(equiv_url_a));
      EXPECT_TRUE(equiv_a.IsFirstParty(equiv_url_b));
      EXPECT_TRUE(equiv_b.IsFirstParty(equiv_url_a));
      EXPECT_TRUE(equiv_b.IsFirstParty(equiv_url_b));
    }

    for (const GURL& other_url : distinct) {
      SiteForCookies other = SiteForCookies::FromUrl(other_url);
      EXPECT_EQ(
          other.RepresentativeUrl().spec(),
          base::StrCat({other_url.scheme(), "://", other_url.host(), "/"}));

      EXPECT_FALSE(equiv_a.IsEquivalent(other));
      EXPECT_FALSE(other.IsEquivalent(equiv_a));
      EXPECT_FALSE(equiv_a.IsFirstParty(other_url))
          << equiv_a.ToDebugString() << " " << other_url.spec();
      EXPECT_FALSE(other.IsFirstParty(equiv_url_a));

      EXPECT_TRUE(other.IsFirstParty(other_url));
    }
  }
}

TEST(SiteForCookiesTest, Default) {
  SiteForCookies should_match_none;
  EXPECT_FALSE(should_match_none.IsFirstParty(GURL("http://example.com")));
  EXPECT_FALSE(should_match_none.IsFirstParty(GURL("file:///home/me/.bashrc")));
  EXPECT_FALSE(should_match_none.IsFirstParty(GURL::EmptyGURL()));

  // Before SiteForCookies existed, empty URL would represent match-none
  EXPECT_TRUE(should_match_none.IsEquivalent(
      SiteForCookies::FromUrl(GURL::EmptyGURL())));
  EXPECT_TRUE(should_match_none.RepresentativeUrl().is_empty());
  EXPECT_TRUE(should_match_none.IsEquivalent(
      SiteForCookies::FromOrigin(url::Origin())));

  EXPECT_EQ("", should_match_none.scheme());
  EXPECT_EQ("SiteForCookies: {scheme=; registrable_domain=}",
            should_match_none.ToDebugString());
}

TEST(SiteForCookiesTest, Basic) {
  std::vector<GURL> equivalent = {
      GURL("https://example.com"),
      GURL("http://sub1.example.com:42/something"),
      GURL("ws://sub2.example.com/something"),
      // This one is disputable.
      GURL("file://example.com/helo"),
  };

  std::vector<GURL> distinct = {GURL("https://example.org"),
                                GURL("http://com/i_am_a_tld")};

  TestEquivalentAndDistinct(equivalent, distinct, "example.com");
}

TEST(SiteForCookiesTest, File) {
  std::vector<GURL> equivalent = {GURL("file:///a/b/c"),
                                  GURL("file:///etc/shaaadow")};

  std::vector<GURL> distinct = {GURL("file://nonlocal/file.txt")};

  TestEquivalentAndDistinct(equivalent, distinct, "");
}

TEST(SiteForCookiesTest, Extension) {
  url::ScopedSchemeRegistryForTests scoped_registry;
  url::AddStandardScheme("chrome-extension", url::SCHEME_WITH_HOST);
  std::vector<GURL> equivalent = {GURL("chrome-extension://abc/"),
                                  GURL("chrome-extension://abc/foo.txt"),
                                  // This one is disputable.
                                  GURL("file://abc/bar.txt")};

  std::vector<GURL> distinct = {GURL("chrome-extension://def")};

  TestEquivalentAndDistinct(equivalent, distinct, "abc");
}

TEST(SiteForCookiesTest, NonStandard) {
  // If we don't register the scheme, nothing matches, even identical ones
  std::vector<GURL> equivalent;
  std::vector<GURL> distinct = {GURL("non-standard://abc"),
                                GURL("non-standard://abc"),
                                GURL("non-standard://def")};

  // Last parameter is "" since GURL doesn't put the hostname in if
  // the URL is non-standard.
  TestEquivalentAndDistinct(equivalent, distinct, "");
}

TEST(SiteForCookiesTest, Blob) {
  // This case isn't really well-specified and is inconsistent between
  // different user agents; the behavior chosen here was to be more
  // consistent between url and origin handling.
  //
  // Thanks file API spec for the sample blob URL.
  SiteForCookies from_blob = SiteForCookies::FromUrl(
      GURL("blob:https://example.org/9115d58c-bcda-ff47-86e5-083e9a2153041"));

  EXPECT_TRUE(from_blob.IsFirstParty(GURL("http://sub.example.org/resource")));
  EXPECT_EQ("https", from_blob.scheme());
  EXPECT_EQ("SiteForCookies: {scheme=https; registrable_domain=example.org}",
            from_blob.ToDebugString());
  EXPECT_EQ("https://example.org/", from_blob.RepresentativeUrl().spec());
  EXPECT_TRUE(from_blob.IsEquivalent(
      SiteForCookies::FromUrl(GURL("http://www.example.org:631"))));
}

TEST(SiteForCookiesTest, Wire) {
  SiteForCookies out;

  // Empty one.
  EXPECT_TRUE(SiteForCookies::FromWire("", "", &out));
  EXPECT_TRUE(out.IsNull());

  // Not a valid scheme.
  EXPECT_FALSE(SiteForCookies::FromWire("aH", "example.com", &out));
  EXPECT_TRUE(out.IsNull());

  // Not a eTLD + 1 (or something hosty).
  EXPECT_FALSE(SiteForCookies::FromWire("http", "sub.example.com", &out));
  EXPECT_TRUE(out.IsNull());

  // This is fine, though.
  EXPECT_TRUE(SiteForCookies::FromWire("https", "127.0.0.1", &out));
  EXPECT_FALSE(out.IsNull());
  EXPECT_EQ("SiteForCookies: {scheme=https; registrable_domain=127.0.0.1}",
            out.ToDebugString());

  // As is actual eTLD+1.
  EXPECT_TRUE(SiteForCookies::FromWire("wss", "example.com", &out));
  EXPECT_FALSE(out.IsNull());
  EXPECT_EQ("SiteForCookies: {scheme=wss; registrable_domain=example.com}",
            out.ToDebugString());
}

}  // namespace
}  // namespace net
