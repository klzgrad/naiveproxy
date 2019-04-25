// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_ICU_TEST_UTIL_H_
#define BASE_TEST_ICU_TEST_UTIL_H_

#include <string>

#include "base/macros.h"

namespace base {
namespace test {

// In unit tests, prefer ScopedRestoreICUDefaultLocale over
// calling base::i18n::SetICUDefaultLocale() directly. This scoper makes it
// harder to accidentally forget to reset the locale.
class ScopedRestoreICUDefaultLocale {
 public:
  ScopedRestoreICUDefaultLocale();
  explicit ScopedRestoreICUDefaultLocale(const std::string& locale);
  ~ScopedRestoreICUDefaultLocale();

 private:
  const std::string default_locale_;

  DISALLOW_COPY_AND_ASSIGN(ScopedRestoreICUDefaultLocale);
};

void InitializeICUForTesting();

}  // namespace test
}  // namespace base

#endif  // BASE_TEST_ICU_TEST_UTIL_H_
