// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/i18n.h"

#include <windows.h>

#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace {

typedef decltype(::GetSystemPreferredUILanguages)* GetPreferredUILanguages_Fn;

bool GetPreferredUILanguageList(GetPreferredUILanguages_Fn function,
                                ULONG flags,
                                std::vector<base::string16>* languages) {
  DCHECK_EQ(0U, (flags & (MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME)));
  const ULONG call_flags = flags | MUI_LANGUAGE_NAME;
  ULONG language_count = 0;
  ULONG buffer_length = 0;
  if (!function(call_flags, &language_count, nullptr, &buffer_length) ||
      0 == buffer_length) {
    DPCHECK(0 == buffer_length)
        << "Failed getting size of preferred UI languages.";
    return false;
  }

  base::string16 buffer(buffer_length, '\0');
  if (!function(call_flags, &language_count, base::as_writable_wcstr(buffer),
                &buffer_length) ||
      0 == language_count) {
    DPCHECK(0 == language_count) << "Failed getting preferred UI languages.";
    return false;
  }

  // Split string on NUL characters.
  *languages =
      base::SplitString(buffer, base::string16(1, '\0'), base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);
  DCHECK_EQ(languages->size(), language_count);
  return true;
}

}  // namespace

namespace base {
namespace win {
namespace i18n {

bool GetUserPreferredUILanguageList(std::vector<base::string16>* languages) {
  DCHECK(languages);
  return GetPreferredUILanguageList(::GetUserPreferredUILanguages, 0,
                                    languages);
}

bool GetThreadPreferredUILanguageList(std::vector<base::string16>* languages) {
  DCHECK(languages);
  return GetPreferredUILanguageList(
      ::GetThreadPreferredUILanguages,
      MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK, languages);
}

}  // namespace i18n
}  // namespace win
}  // namespace base
