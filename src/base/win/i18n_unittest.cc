// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains unit tests for Windows internationalization funcs.

#include <stddef.h>
#include <string.h>

#include "base/strings/string_util.h"
#include "base/win/i18n.h"
#include "base/win/windows_version.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {
namespace i18n {

// Tests that at least one user preferred UI language can be obtained.
TEST(I18NTest, GetUserPreferredUILanguageList) {
  std::vector<base::string16> languages;
  EXPECT_TRUE(GetUserPreferredUILanguageList(&languages));
  EXPECT_FALSE(languages.empty());
  for (const auto& language : languages) {
    EXPECT_FALSE(language.empty());
    // Ensure there's no extra trailing 0 characters.
    EXPECT_EQ(language.size(), wcslen(base::as_wcstr(language)));
  }
}

// Tests that at least one thread preferred UI language can be obtained.
TEST(I18NTest, GetThreadPreferredUILanguageList) {
  std::vector<base::string16> languages;
  EXPECT_TRUE(GetThreadPreferredUILanguageList(&languages));
  EXPECT_FALSE(languages.empty());
  for (const auto& language : languages) {
    EXPECT_FALSE(language.empty());
    EXPECT_EQ(language.size(), wcslen(base::as_wcstr(language)));
  }
}

}  // namespace i18n
}  // namespace win
}  // namespace base
