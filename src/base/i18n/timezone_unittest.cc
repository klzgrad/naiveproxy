// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/timezone.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

TEST(TimezoneTest, CountryCodeForCurrentTimezone) {
  std::string country_code = CountryCodeForCurrentTimezone();
  // On some systems (such as Android or some flavors of Linux), ICU may come up
  // empty. With https://chromium-review.googlesource.com/c/512282/ , ICU will
  // not fail any more. See also http://bugs.icu-project.org/trac/ticket/13208 .
  // Even with that, ICU returns '001' (world) for region-agnostic timezones
  // such as Etc/UTC and |CountryCodeForCurrentTimezone| returns an empty
  // string so that the next fallback can be tried by a customer.
  // TODO(jshin): Revise this to test for actual timezones using
  // use ScopedRestoreICUDefaultTimezone.
  if (!country_code.empty())
    EXPECT_EQ(2U, country_code.size()) << "country_code = " << country_code;
}

}  // namespace
}  // namespace base
