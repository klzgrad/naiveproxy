// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/case_conversion.h"

#include <stdint.h>

#include <string>
#include <string_view>

#include "base/numerics/safe_conversions.h"
#include "base/strings/string_util.h"
#include "third_party/icu/source/common/unicode/uchar.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/common/unicode/ustring.h"

namespace base::i18n {

namespace {

// Provides a uniform interface for upper/lower/folding which take take
// slightly varying parameters.
typedef int32_t (*CaseMapperFunction)(UChar* dest,
                                      int32_t dest_capacity,
                                      const UChar* src,
                                      int32_t src_length,
                                      UErrorCode* error);

int32_t ToUpperMapper(UChar* dest,
                      int32_t dest_capacity,
                      const UChar* src,
                      int32_t src_length,
                      UErrorCode* error) {
  // Use default locale.
  return u_strToUpper(dest, dest_capacity, src, src_length, nullptr, error);
}

int32_t ToLowerMapper(UChar* dest,
                      int32_t dest_capacity,
                      const UChar* src,
                      int32_t src_length,
                      UErrorCode* error) {
  // Use default locale.
  return u_strToLower(dest, dest_capacity, src, src_length, nullptr, error);
}

int32_t FoldCaseMapper(UChar* dest,
                       int32_t dest_capacity,
                       const UChar* src,
                       int32_t src_length,
                       UErrorCode* error) {
  return u_strFoldCase(dest, dest_capacity, src, src_length,
                       U_FOLD_CASE_DEFAULT, error);
}

// Provides similar functionality as UnicodeString::caseMap but on
// std::u16string.
std::u16string CaseMap(std::u16string_view string,
                       CaseMapperFunction case_mapper) {
  std::u16string dest;
  if (string.empty()) {
    return dest;
  }

  // Provide an initial guess that the string length won't change. The typical
  // strings we use will very rarely change length in this process, so don't
  // optimize for that case.
  dest.resize(string.size());

  UErrorCode error;
  do {
    error = U_ZERO_ERROR;

    // ICU won't terminate the string if there's not enough room for the null
    // terminator, but will otherwise. So we don't need to save room for that.
    // Don't use WriteInto, which assumes null terminators.
    int32_t new_length = case_mapper(
        &dest[0], saturated_cast<int32_t>(dest.size()), string.data(),
        saturated_cast<int32_t>(string.size()), &error);
    dest.resize(new_length);
  } while (error == U_BUFFER_OVERFLOW_ERROR);
  return dest;
}

}  // namespace

std::u16string ToLower(std::u16string_view string) {
  return CaseMap(string, &ToLowerMapper);
}

std::u16string ToUpper(std::u16string_view string) {
  return CaseMap(string, &ToUpperMapper);
}

std::u16string FoldCase(std::u16string_view string) {
  return CaseMap(string, &FoldCaseMapper);
}

}  // namespace base::i18n
