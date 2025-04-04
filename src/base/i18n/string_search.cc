// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/string_search.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "third_party/icu/source/i18n/unicode/usearch.h"

namespace base::i18n {

FixedPatternStringSearch::FixedPatternStringSearch(std::u16string find_this,
                                                   bool case_sensitive)
    : find_this_(std::move(find_this)) {
  UErrorCode status = U_ZERO_ERROR;
  search_ =
      usearch_open(find_this_.data(), find_this_.size(),
                   // `usearch_open()` requires a valid string argument to be
                   // searched, even if we want to set it by `usearch_setText()`
                   // afterwards. So just provide `find_this_` again.
                   find_this_.data(), find_this_.size(), uloc_getDefault(),
                   /*breakiter=*/nullptr, &status);
  if (U_SUCCESS(status)) {
    // http://icu-project.org/apiref/icu4c40/ucol_8h.html#6a967f36248b0a1bc7654f538ee8ba96
    // Set comparison level to UCOL_PRIMARY to ignore secondary and tertiary
    // differences. Set comparison level to UCOL_TERTIARY to include all
    // comparison differences.
    // Diacritical differences on the same base letter represent a
    // secondary difference.
    // Uppercase and lowercase versions of the same character represents a
    // tertiary difference.
    UCollator* collator = usearch_getCollator(search_);
    ucol_setStrength(collator, case_sensitive ? UCOL_TERTIARY : UCOL_PRIMARY);
    usearch_reset(search_);
  }
}

FixedPatternStringSearch::~FixedPatternStringSearch() {
  if (search_) {
    usearch_close(search_.ExtractAsDangling());
  }
}

bool FixedPatternStringSearch::Search(std::u16string_view in_this,
                                      size_t* match_index,
                                      size_t* match_length,
                                      bool forward_search) {
  UErrorCode status = U_ZERO_ERROR;
  usearch_setText(search_, in_this.data(), in_this.size(), &status);

  // Default to basic substring search if usearch fails. According to
  // http://icu-project.org/apiref/icu4c/usearch_8h.html, usearch_open will fail
  // if either |find_this| or |in_this| are empty. In either case basic
  // substring search will give the correct return value.
  if (!U_SUCCESS(status)) {
    size_t index = in_this.find(find_this_);
    if (index == std::u16string::npos) {
      return false;
    }
    if (match_index) {
      *match_index = index;
    }
    if (match_length) {
      *match_length = find_this_.size();
    }
    return true;
  }

  int32_t index = forward_search ? usearch_first(search_, &status)
                                 : usearch_last(search_, &status);
  if (!U_SUCCESS(status) || index == USEARCH_DONE) {
    return false;
  }
  if (match_index) {
    *match_index = static_cast<size_t>(index);
  }
  if (match_length) {
    *match_length = static_cast<size_t>(usearch_getMatchedLength(search_));
  }
  return true;
}

FixedPatternStringSearchIgnoringCaseAndAccents::
    FixedPatternStringSearchIgnoringCaseAndAccents(std::u16string find_this)
    : base_search_(std::move(find_this), /*case_sensitive=*/false) {}

bool FixedPatternStringSearchIgnoringCaseAndAccents::Search(
    std::u16string_view in_this,
    size_t* match_index,
    size_t* match_length) {
  return base_search_.Search(in_this, match_index, match_length,
                             /*forward_search=*/true);
}

bool StringSearchIgnoringCaseAndAccents(std::u16string find_this,
                                        std::u16string_view in_this,
                                        size_t* match_index,
                                        size_t* match_length) {
  return FixedPatternStringSearchIgnoringCaseAndAccents(std::move(find_this))
      .Search(in_this, match_index, match_length);
}

bool StringSearch(std::u16string find_this,
                  std::u16string_view in_this,
                  size_t* match_index,
                  size_t* match_length,
                  bool case_sensitive,
                  bool forward_search) {
  return FixedPatternStringSearch(std::move(find_this), case_sensitive)
      .Search(in_this, match_index, match_length, forward_search);
}

RepeatingStringSearch::RepeatingStringSearch(std::u16string find_this,
                                             std::u16string in_this,
                                             bool case_sensitive)
    : find_this_(std::move(find_this)), in_this_(std::move(in_this)) {
  std::string locale = uloc_getDefault();
  UErrorCode status = U_ZERO_ERROR;
  search_ = usearch_open(find_this_.data(), find_this_.size(), in_this_.data(),
                         in_this_.size(), locale.data(), /*breakiter=*/nullptr,
                         &status);
  DCHECK(U_SUCCESS(status));
  if (U_SUCCESS(status)) {
    // http://icu-project.org/apiref/icu4c40/ucol_8h.html#6a967f36248b0a1bc7654f538ee8ba96
    // Set comparison level to UCOL_PRIMARY to ignore secondary and tertiary
    // differences. Set comparison level to UCOL_TERTIARY to include all
    // comparison differences.
    // Diacritical differences on the same base letter represent a
    // secondary difference.
    // Uppercase and lowercase versions of the same character represents a
    // tertiary difference.
    UCollator* collator = usearch_getCollator(search_);
    ucol_setStrength(collator, case_sensitive ? UCOL_TERTIARY : UCOL_PRIMARY);
    usearch_reset(search_);
  }
}

RepeatingStringSearch::~RepeatingStringSearch() {
  if (search_) {
    usearch_close(search_.ExtractAsDangling());
  }
}

bool RepeatingStringSearch::NextMatchResult(int& match_index,
                                            int& match_length) {
  UErrorCode status = U_ZERO_ERROR;
  const int match_start = usearch_next(search_, &status);
  if (U_FAILURE(status) || match_start == USEARCH_DONE) {
    return false;
  }
  DCHECK(U_SUCCESS(status));
  match_index = match_start;
  match_length = usearch_getMatchedLength(search_);
  return true;
}

}  // namespace base::i18n
