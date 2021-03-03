// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_STRINGS_STRING_SPLIT_WIN_H_
#define BASE_STRINGS_STRING_SPLIT_WIN_H_

#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/strings/string16.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"

namespace base {

// The following section contains overloads of the cross-platform APIs for
// std::wstring and base::WStringPiece. These are only enabled if std::wstring
// and base::string16 are distinct types, as otherwise this would result in an
// ODR violation.
// TODO(crbug.com/911896): Remove those guards once base::string16 is
// std::u16string.
#if defined(BASE_STRING16_IS_STD_U16STRING)
BASE_EXPORT std::vector<std::wstring> SplitString(WStringPiece input,
                                                  WStringPiece separators,
                                                  WhitespaceHandling whitespace,
                                                  SplitResult result_type)
    WARN_UNUSED_RESULT;

BASE_EXPORT std::vector<WStringPiece> SplitStringPiece(
    WStringPiece input,
    WStringPiece separators,
    WhitespaceHandling whitespace,
    SplitResult result_type) WARN_UNUSED_RESULT;

BASE_EXPORT std::vector<std::wstring> SplitStringUsingSubstr(
    WStringPiece input,
    WStringPiece delimiter,
    WhitespaceHandling whitespace,
    SplitResult result_type) WARN_UNUSED_RESULT;

BASE_EXPORT std::vector<WStringPiece> SplitStringPieceUsingSubstr(
    WStringPiece input,
    WStringPiece delimiter,
    WhitespaceHandling whitespace,
    SplitResult result_type) WARN_UNUSED_RESULT;
#endif

}  // namespace base

#endif  // BASE_STRINGS_STRING_SPLIT_WIN_H_
