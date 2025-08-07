// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/string_compare.h"

#include <string_view>

#include "base/check.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/icu/source/common/unicode/unistr.h"

namespace base::i18n {

// Compares the character data stored in two different std::u16string strings by
// specified Collator instance.
UCollationResult CompareString16WithCollator(const icu::Collator& collator,
                                             std::u16string_view lhs,
                                             std::u16string_view rhs) {
  UErrorCode error = U_ZERO_ERROR;
  UCollationResult result = collator.compare(
      icu::UnicodeString(false, lhs.data(), static_cast<int>(lhs.length())),
      icu::UnicodeString(false, rhs.data(), static_cast<int>(rhs.length())),
      error);
  DCHECK(U_SUCCESS(error));
  return result;
}

}  // namespace base::i18n
