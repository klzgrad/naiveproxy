// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/rtl.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string_view>

#include "base/check_op.h"
#include "base/command_line.h"
#include "base/containers/fixed_flat_set.h"
#include "base/files/file_path.h"
#include "base/i18n/base_i18n_switches.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/common/unicode/uchar.h"
#include "third_party/icu/source/common/unicode/uscript.h"
#include "third_party/icu/source/i18n/unicode/coll.h"

#if BUILDFLAG(IS_IOS)
#include "base/debug/crash_logging.h"
#include "base/ios/ios_util.h"
#endif

namespace {

// Extract language, country and variant, but ignore keywords.  For example,
// en-US, ca@valencia, ca-ES@valencia.
std::string GetLocaleString(const icu::Locale& locale) {
  const char* language = locale.getLanguage();
  const char* country = locale.getCountry();
  const char* variant = locale.getVariant();
  const char* script = locale.getScript();

  std::string result =
      (language != nullptr && *language != '\0') ? language : "und";

  if (script != nullptr && *script != '\0') {
    result += '-';
    result += script;
  }

  if (country != nullptr && *country != '\0') {
    result += '-';
    result += country;
  }

  if (variant != nullptr && *variant != '\0') {
    result += '@' + base::ToLowerASCII(variant);
  }

  return result;
}

// Returns LEFT_TO_RIGHT or RIGHT_TO_LEFT if |character| has strong
// directionality, returns UNKNOWN_DIRECTION if it doesn't. Please refer to
// http://unicode.org/reports/tr9/ for more information.
base::i18n::TextDirection GetCharacterDirection(UChar32 character) {
  static bool has_switch = base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kForceTextDirection);
  if (has_switch) {
    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    std::string force_flag =
        command_line->GetSwitchValueASCII(switches::kForceTextDirection);

    if (force_flag == switches::kForceDirectionRTL) {
      return base::i18n::RIGHT_TO_LEFT;
    }
    if (force_flag == switches::kForceDirectionLTR) {
      return base::i18n::LEFT_TO_RIGHT;
    }
  }
  // Now that we have the character, we use ICU in order to query for the
  // appropriate Unicode BiDi character type.
  int32_t property = u_getIntPropertyValue(character, UCHAR_BIDI_CLASS);
  switch (property) {
    case U_RIGHT_TO_LEFT:
    case U_RIGHT_TO_LEFT_ARABIC:
    case U_RIGHT_TO_LEFT_EMBEDDING:
    case U_RIGHT_TO_LEFT_OVERRIDE:
      return base::i18n::RIGHT_TO_LEFT;
    case U_LEFT_TO_RIGHT:
    case U_LEFT_TO_RIGHT_EMBEDDING:
    case U_LEFT_TO_RIGHT_OVERRIDE:
      return base::i18n::LEFT_TO_RIGHT;
  }
  return base::i18n::UNKNOWN_DIRECTION;
}

}  // namespace

namespace base::i18n {

// Represents the locale-specific ICU text direction.
static TextDirection g_icu_text_direction = UNKNOWN_DIRECTION;

// Convert the ICU default locale to a string.
std::string GetConfiguredLocale() {
  return GetLocaleString(icu::Locale::getDefault());
}

// Convert the ICU canonicalized locale to a string.
std::string GetCanonicalLocale(const std::string& locale) {
  return GetLocaleString(icu::Locale::createCanonical(locale.c_str()));
}

// Convert Chrome locale name to ICU locale name
std::string ICULocaleName(const std::string& locale_string) {
  // If not Spanish, just return it.
  if (locale_string.substr(0, 2) != "es") {
    return locale_string;
  }
  // Expand es to es-ES.
  if (EqualsCaseInsensitiveASCII(locale_string, "es")) {
    return "es-ES";
  }
  // Map es-419 (Latin American Spanish) to es-FOO depending on the system
  // locale.  If it's es-RR other than es-ES, map to es-RR. Otherwise, map
  // to es-MX (the most populous in Spanish-speaking Latin America).
  if (EqualsCaseInsensitiveASCII(locale_string, "es-419")) {
    const icu::Locale& locale = icu::Locale::getDefault();
    std::string language = locale.getLanguage();
    const char* country = locale.getCountry();
    if (EqualsCaseInsensitiveASCII(language, "es") &&
        !EqualsCaseInsensitiveASCII(country, "es")) {
      language += '-';
      language += country;
      return language;
    }
    return "es-MX";
  }
  // Currently, Chrome has only "es" and "es-419", but later we may have
  // more specific "es-RR".
  return locale_string;
}

void SetICUDefaultLocale(const std::string& locale_string) {
#if BUILDFLAG(IS_IOS)
  static base::debug::CrashKeyString* crash_key_locale =
      base::debug::AllocateCrashKeyString("icu_locale_input",
                                          base::debug::CrashKeySize::Size256);
  base::debug::SetCrashKeyString(crash_key_locale, locale_string);
#endif
  icu::Locale locale(ICULocaleName(locale_string).c_str());
  UErrorCode error_code = U_ZERO_ERROR;
  const char* lang = locale.getLanguage();
  if (lang != nullptr && *lang != '\0') {
    icu::Locale::setDefault(locale, error_code);
  } else {
    LOG(ERROR) << "Failed to set the ICU default locale to " << locale_string
               << ". Falling back to en-US.";
    icu::Locale::setDefault(icu::Locale::getUS(), error_code);
  }
  g_icu_text_direction = UNKNOWN_DIRECTION;
}

bool IsRTL() {
  return ICUIsRTL();
}

void SetRTLForTesting(bool rtl) {
  SetICUDefaultLocale(rtl ? "he" : "en");
  DCHECK_EQ(rtl, IsRTL());
}

bool ICUIsRTL() {
  if (g_icu_text_direction == UNKNOWN_DIRECTION) {
    const icu::Locale& locale = icu::Locale::getDefault();
    g_icu_text_direction = GetTextDirectionForLocaleInStartUp(locale.getName());
  }
  return g_icu_text_direction == RIGHT_TO_LEFT;
}

TextDirection GetForcedTextDirection() {
// On iOS, check for RTL forcing.
#if BUILDFLAG(IS_IOS)
  if (base::ios::IsInForcedRTL()) {
    return base::i18n::RIGHT_TO_LEFT;
  }
#endif

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kForceUIDirection)) {
    std::string force_flag =
        command_line->GetSwitchValueASCII(switches::kForceUIDirection);

    if (force_flag == switches::kForceDirectionLTR) {
      return base::i18n::LEFT_TO_RIGHT;
    }

    if (force_flag == switches::kForceDirectionRTL) {
      return base::i18n::RIGHT_TO_LEFT;
    }
  }

  return base::i18n::UNKNOWN_DIRECTION;
}

TextDirection GetTextDirectionForLocaleInStartUp(const char* locale_name) {
  // Check for direction forcing.
  TextDirection forced_direction = GetForcedTextDirection();
  if (forced_direction != UNKNOWN_DIRECTION) {
    return forced_direction;
  }

  CHECK(locale_name && locale_name[0]);

  static constexpr auto kRtlLanguageCodes =
      base::MakeFixedFlatSet<std::string_view>({"ar", "fa", "he", "iw", "ur"});
  std::vector<std::string_view> locale_split =
      SplitStringPiece(locale_name, "-_", KEEP_WHITESPACE, SPLIT_WANT_ALL);
  std::string_view language_code = locale_split[0];
  return kRtlLanguageCodes.contains(language_code) ? RIGHT_TO_LEFT
                                                   : LEFT_TO_RIGHT;
}

TextDirection GetTextDirectionForLocale(const char* locale_name) {
  // Check for direction forcing.
  TextDirection forced_direction = GetForcedTextDirection();
  if (forced_direction != UNKNOWN_DIRECTION) {
    return forced_direction;
  }

  UErrorCode status = U_ZERO_ERROR;
  ULayoutType layout_dir = uloc_getCharacterOrientation(locale_name, &status);
  DCHECK(U_SUCCESS(status));
  // Treat anything other than RTL as LTR.
  return (layout_dir != ULOC_LAYOUT_RTL) ? LEFT_TO_RIGHT : RIGHT_TO_LEFT;
}

TextDirection GetFirstStrongCharacterDirection(std::u16string_view text) {
  size_t length = text.length();
  size_t position = 0;
  while (position < length) {
    UChar32 character;
    size_t next_position = position;
    U16_NEXT(text, next_position, length, character);
    TextDirection direction = GetCharacterDirection(character);
    if (direction != UNKNOWN_DIRECTION) {
      return direction;
    }
    position = next_position;
  }
  return LEFT_TO_RIGHT;
}

TextDirection GetLastStrongCharacterDirection(std::u16string_view text) {
  size_t position = text.length();
  while (position > 0) {
    UChar32 character;
    size_t prev_position = position;
    U16_PREV(text, 0, prev_position, character);
    TextDirection direction = GetCharacterDirection(character);
    if (direction != UNKNOWN_DIRECTION) {
      return direction;
    }
    position = prev_position;
  }
  return LEFT_TO_RIGHT;
}

TextDirection GetStringDirection(std::u16string_view text) {
  size_t length = text.length();
  size_t position = 0;

  TextDirection result(UNKNOWN_DIRECTION);
  while (position < length) {
    UChar32 character;
    size_t next_position = position;
    U16_NEXT(text, next_position, length, character);
    TextDirection direction = GetCharacterDirection(character);
    if (direction != UNKNOWN_DIRECTION) {
      if (result != UNKNOWN_DIRECTION && result != direction) {
        return UNKNOWN_DIRECTION;
      }
      result = direction;
    }
    position = next_position;
  }

  // Handle the case of a string not containing any strong directionality
  // characters defaulting to LEFT_TO_RIGHT.
  return (result == UNKNOWN_DIRECTION) ? LEFT_TO_RIGHT : result;
}

#if BUILDFLAG(IS_WIN)
bool AdjustStringForLocaleDirection(std::u16string* text) {
  if (!IsRTL() || text->empty()) {
    return false;
  }

  // Marking the string as LTR if the locale is RTL and the string does not
  // contain strong RTL characters. Otherwise, mark the string as RTL.
  bool has_rtl_chars = StringContainsStrongRTLChars(*text);
  if (!has_rtl_chars) {
    WrapStringWithLTRFormatting(text);
  } else {
    WrapStringWithRTLFormatting(text);
  }

  return true;
}

bool UnadjustStringForLocaleDirection(std::u16string* text) {
  if (!IsRTL() || text->empty()) {
    return false;
  }

  *text = StripWrappingBidiControlCharacters(*text);
  return true;
}
#else
bool AdjustStringForLocaleDirection(std::u16string* text) {
  // On OS X & GTK the directionality of a label is determined by the first
  // strongly directional character.
  // However, we want to make sure that in an LTR-language-UI all strings are
  // left aligned and vice versa.
  // A problem can arise if we display a string which starts with user input.
  // User input may be of the opposite directionality to the UI. So the whole
  // string will be displayed in the opposite directionality, e.g. if we want to
  // display in an LTR UI [such as US English]:
  //
  // EMAN_NOISNETXE is now installed.
  //
  // Since EXTENSION_NAME begins with a strong RTL char, the label's
  // directionality will be set to RTL and the string will be displayed visually
  // as:
  //
  // .is now installed EMAN_NOISNETXE
  //
  // In order to solve this issue, we prepend an LRM to the string. An LRM is a
  // strongly directional LTR char.
  // We also append an LRM at the end, which ensures that we're in an LTR
  // context.

  // Unlike Windows, Linux and OS X can correctly display RTL glyphs out of the
  // box so there is no issue with displaying zero-width bidi control characters
  // on any system.  Thus no need for the !IsRTL() check here.
  if (text->empty()) {
    return false;
  }

  bool ui_direction_is_rtl = IsRTL();

  bool has_rtl_chars = StringContainsStrongRTLChars(*text);
  if (!ui_direction_is_rtl && has_rtl_chars) {
    WrapStringWithRTLFormatting(text);
    text->insert(static_cast<size_t>(0), static_cast<size_t>(1),
                 kLeftToRightMark);
    text->push_back(kLeftToRightMark);
  } else if (ui_direction_is_rtl && has_rtl_chars) {
    WrapStringWithRTLFormatting(text);
    text->insert(static_cast<size_t>(0), static_cast<size_t>(1),
                 kRightToLeftMark);
    text->push_back(kRightToLeftMark);
  } else if (ui_direction_is_rtl) {
    WrapStringWithLTRFormatting(text);
    text->insert(static_cast<size_t>(0), static_cast<size_t>(1),
                 kRightToLeftMark);
    text->push_back(kRightToLeftMark);
  } else {
    return false;
  }

  return true;
}

bool UnadjustStringForLocaleDirection(std::u16string* text) {
  if (text->empty()) {
    return false;
  }

  size_t begin_index = 0;
  char16_t begin = text->at(begin_index);
  if (begin == kLeftToRightMark || begin == kRightToLeftMark) {
    ++begin_index;
  }

  size_t end_index = text->length() - 1;
  char16_t end = text->at(end_index);
  if (end == kLeftToRightMark || end == kRightToLeftMark) {
    --end_index;
  }

  std::u16string unmarked_text =
      text->substr(begin_index, end_index - begin_index + 1);
  *text = StripWrappingBidiControlCharacters(unmarked_text);
  return true;
}

#endif  // !BUILDFLAG(IS_WIN)

void EnsureTerminatedDirectionalFormatting(std::u16string* text) {
  int count = 0;
  for (auto c : *text) {
    if (c == kLeftToRightEmbeddingMark || c == kRightToLeftEmbeddingMark ||
        c == kLeftToRightOverride || c == kRightToLeftOverride) {
      ++count;
    } else if (c == kPopDirectionalFormatting && count > 0) {
      --count;
    }
  }
  for (int j = 0; j < count; j++) {
    text->push_back(kPopDirectionalFormatting);
  }
}

void SanitizeUserSuppliedString(std::u16string* text) {
  EnsureTerminatedDirectionalFormatting(text);
  AdjustStringForLocaleDirection(text);
}

bool StringContainsStrongRTLChars(std::u16string_view text) {
  size_t length = text.length();
  size_t position = 0;
  while (position < length) {
    UChar32 character;
    size_t next_position = position;
    U16_NEXT(text, next_position, length, character);

    // Now that we have the character, we use ICU in order to query for the
    // appropriate Unicode BiDi character type.
    int32_t property = u_getIntPropertyValue(character, UCHAR_BIDI_CLASS);
    if ((property == U_RIGHT_TO_LEFT) || (property == U_RIGHT_TO_LEFT_ARABIC)) {
      return true;
    }

    position = next_position;
  }

  return false;
}

void WrapStringWithLTRFormatting(std::u16string* text) {
  if (text->empty()) {
    return;
  }

  // Inserting an LRE (Left-To-Right Embedding) mark as the first character.
  text->insert(static_cast<size_t>(0), static_cast<size_t>(1),
               kLeftToRightEmbeddingMark);

  // Inserting a PDF (Pop Directional Formatting) mark as the last character.
  text->push_back(kPopDirectionalFormatting);
}

void WrapStringWithRTLFormatting(std::u16string* text) {
  if (text->empty()) {
    return;
  }

  // Inserting an RLE (Right-To-Left Embedding) mark as the first character.
  text->insert(static_cast<size_t>(0), static_cast<size_t>(1),
               kRightToLeftEmbeddingMark);

  // Inserting a PDF (Pop Directional Formatting) mark as the last character.
  text->push_back(kPopDirectionalFormatting);
}

void WrapPathWithLTRFormatting(const FilePath& path,
                               std::u16string* rtl_safe_path) {
  // Wrap the overall path with LRE-PDF pair which essentialy marks the
  // string as a Left-To-Right string.
  // Inserting an LRE (Left-To-Right Embedding) mark as the first character.
  rtl_safe_path->push_back(kLeftToRightEmbeddingMark);
#if BUILDFLAG(IS_APPLE)
  rtl_safe_path->append(UTF8ToUTF16(path.value()));
#elif BUILDFLAG(IS_WIN)
  rtl_safe_path->append(AsString16(path.value()));
#else  // BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE)
  std::wstring wide_path = base::SysNativeMBToWide(path.value());
  rtl_safe_path->append(WideToUTF16(wide_path));
#endif
  // Inserting a PDF (Pop Directional Formatting) mark as the last character.
  rtl_safe_path->push_back(kPopDirectionalFormatting);
}

std::u16string GetDisplayStringInLTRDirectionality(std::u16string_view text) {
  // Always wrap the string in RTL UI (it may be appended to RTL string).
  // Also wrap strings with an RTL first strong character direction in LTR UI.
  std::u16string text_mutable(text);
  if (IsRTL() || GetFirstStrongCharacterDirection(text) == RIGHT_TO_LEFT) {
    WrapStringWithLTRFormatting(&text_mutable);
  }
  return text_mutable;
}

std::u16string StripWrappingBidiControlCharacters(std::u16string_view text) {
  if (text.empty()) {
    return std::u16string(text);
  }
  size_t begin_index = 0;
  char16_t begin = text[begin_index];
  if (begin == kLeftToRightEmbeddingMark ||
      begin == kRightToLeftEmbeddingMark || begin == kLeftToRightOverride ||
      begin == kRightToLeftOverride) {
    ++begin_index;
  }
  size_t end_index = text.length() - 1;
  if (text[end_index] == kPopDirectionalFormatting) {
    --end_index;
  }
  return std::u16string(text.substr(begin_index, end_index - begin_index + 1));
}

}  // namespace base::i18n
