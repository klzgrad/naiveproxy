// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <tuple>
#include <vector>

#include "base/macros.h"
#include "base/stl_util.h"
#include "base/test/gtest_util.h"
#include "base/win/embedded_i18n/language_selector.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {
namespace i18n {
namespace {

constexpr const base::char16* kExactMatchCandidates[] = {
    STRING16_LITERAL("am"),     STRING16_LITERAL("ar"),
    STRING16_LITERAL("bg"),     STRING16_LITERAL("bn"),
    STRING16_LITERAL("ca"),     STRING16_LITERAL("cs"),
    STRING16_LITERAL("da"),     STRING16_LITERAL("de"),
    STRING16_LITERAL("el"),     STRING16_LITERAL("en-gb"),
    STRING16_LITERAL("en-us"),  STRING16_LITERAL("es"),
    STRING16_LITERAL("es-419"), STRING16_LITERAL("et"),
    STRING16_LITERAL("fa"),     STRING16_LITERAL("fi"),
    STRING16_LITERAL("fil"),    STRING16_LITERAL("fr"),
    STRING16_LITERAL("gu"),     STRING16_LITERAL("hi"),
    STRING16_LITERAL("hr"),     STRING16_LITERAL("hu"),
    STRING16_LITERAL("id"),     STRING16_LITERAL("it"),
    STRING16_LITERAL("iw"),     STRING16_LITERAL("ja"),
    STRING16_LITERAL("kn"),     STRING16_LITERAL("ko"),
    STRING16_LITERAL("lt"),     STRING16_LITERAL("lv"),
    STRING16_LITERAL("ml"),     STRING16_LITERAL("mr"),
    STRING16_LITERAL("nl"),     STRING16_LITERAL("no"),
    STRING16_LITERAL("pl"),     STRING16_LITERAL("pt-br"),
    STRING16_LITERAL("pt-pt"),  STRING16_LITERAL("ro"),
    STRING16_LITERAL("ru"),     STRING16_LITERAL("sk"),
    STRING16_LITERAL("sl"),     STRING16_LITERAL("sr"),
    STRING16_LITERAL("sv"),     STRING16_LITERAL("sw"),
    STRING16_LITERAL("ta"),     STRING16_LITERAL("te"),
    STRING16_LITERAL("th"),     STRING16_LITERAL("tr"),
    STRING16_LITERAL("uk"),     STRING16_LITERAL("vi"),
    STRING16_LITERAL("zh-cn"),  STRING16_LITERAL("zh-tw")};

constexpr const base::char16* kAliasMatchCandidates[] = {
    STRING16_LITERAL("he"),      STRING16_LITERAL("nb"),
    STRING16_LITERAL("tl"),      STRING16_LITERAL("zh-chs"),
    STRING16_LITERAL("zh-cht"),  STRING16_LITERAL("zh-hans"),
    STRING16_LITERAL("zh-hant"), STRING16_LITERAL("zh-hk"),
    STRING16_LITERAL("zh-mo")};

constexpr const base::char16* kWildcardMatchCandidates[] = {
    STRING16_LITERAL("en-AU"), STRING16_LITERAL("es-CO"),
    STRING16_LITERAL("pt-AB"), STRING16_LITERAL("zh-SG")};

std::vector<LanguageSelector::LangToOffset> MakeLanguageOffsetPairs() {
  std::vector<LanguageSelector::LangToOffset> language_offset_pairs;
  int i = 0;
  for (const base::char16* lang : kExactMatchCandidates) {
    language_offset_pairs.push_back({lang, i++});
  }

  return language_offset_pairs;
}

class TestLanguageSelector : public LanguageSelector {
 public:
  TestLanguageSelector()
      : TestLanguageSelector(std::vector<base::string16>()) {}
  explicit TestLanguageSelector(const std::vector<base::string16>& candidates)
      : TestLanguageSelector(candidates, MakeLanguageOffsetPairs()) {}
  TestLanguageSelector(
      const std::vector<base::string16>& candidates,
      base::span<const LanguageSelector::LangToOffset> languages_to_offset)
      : LanguageSelector(candidates, languages_to_offset) {}
};

}  // namespace

// Test that a language is selected from the system.
TEST(LanguageSelectorTest, DefaultSelection) {
  TestLanguageSelector instance;
  EXPECT_FALSE(instance.matched_candidate().empty());
}

// Test some hypothetical candidate sets.
TEST(LanguageSelectorTest, AssortedSelections) {
  {
    std::vector<base::string16> candidates = {STRING16_LITERAL("fr-BE"),
                                              STRING16_LITERAL("fr"),
                                              STRING16_LITERAL("en")};
    TestLanguageSelector instance(candidates);
    // Expect the exact match to win.
    EXPECT_EQ(STRING16_LITERAL("fr"), instance.matched_candidate());
  }
  {
    std::vector<base::string16> candidates = {STRING16_LITERAL("xx-YY"),
                                              STRING16_LITERAL("cc-Ssss-RR")};
    TestLanguageSelector instance(candidates);
    // Expect the fallback to win.
    EXPECT_EQ(STRING16_LITERAL("en-us"), instance.matched_candidate());
  }
  {
    std::vector<base::string16> candidates = {STRING16_LITERAL("zh-SG"),
                                              STRING16_LITERAL("en-GB")};
    TestLanguageSelector instance(candidates);
    // Expect the alias match to win.
    EXPECT_EQ(STRING16_LITERAL("zh-SG"), instance.matched_candidate());
  }
}

// A fixture for testing sets of single-candidate selections.
class LanguageSelectorMatchCandidateTest
    : public ::testing::TestWithParam<const base::char16*> {};

TEST_P(LanguageSelectorMatchCandidateTest, TestMatchCandidate) {
  TestLanguageSelector instance({GetParam()});
  EXPECT_EQ(GetParam(), instance.matched_candidate());
}

// Test that all existing translations can be found by exact match.
INSTANTIATE_TEST_SUITE_P(TestExactMatches,
                         LanguageSelectorMatchCandidateTest,
                         ::testing::ValuesIn(kExactMatchCandidates));

// Test the alias matches.
INSTANTIATE_TEST_SUITE_P(TestAliasMatches,
                         LanguageSelectorMatchCandidateTest,
                         ::testing::ValuesIn(kAliasMatchCandidates));

// Test a few wildcard matches.
INSTANTIATE_TEST_SUITE_P(TestWildcardMatches,
                         LanguageSelectorMatchCandidateTest,
                         ::testing::ValuesIn(kWildcardMatchCandidates));

// A fixture for testing aliases that match to an expected translation.  The
// first member of the tuple is the expected translation, the second is a
// candidate that should be aliased to the expectation.
class LanguageSelectorAliasTest
    : public ::testing::TestWithParam<
          std::tuple<const base::char16*, const base::char16*>> {};

// Test that the candidate language maps to the aliased translation.
TEST_P(LanguageSelectorAliasTest, AliasesMatch) {
  TestLanguageSelector instance({std::get<1>(GetParam())});
  EXPECT_EQ(std::get<0>(GetParam()), instance.selected_translation());
}

INSTANTIATE_TEST_SUITE_P(
    EnGbAliases,
    LanguageSelectorAliasTest,
    ::testing::Combine(::testing::Values(STRING16_LITERAL("en-gb")),
                       ::testing::Values(STRING16_LITERAL("en-au"),
                                         STRING16_LITERAL("en-ca"),
                                         STRING16_LITERAL("en-nz"),
                                         STRING16_LITERAL("en-za"))));

INSTANTIATE_TEST_SUITE_P(
    IwAliases,
    LanguageSelectorAliasTest,
    ::testing::Combine(::testing::Values(STRING16_LITERAL("iw")),
                       ::testing::Values(STRING16_LITERAL("he"))));

INSTANTIATE_TEST_SUITE_P(
    NoAliases,
    LanguageSelectorAliasTest,
    ::testing::Combine(::testing::Values(STRING16_LITERAL("no")),
                       ::testing::Values(STRING16_LITERAL("nb"))));

INSTANTIATE_TEST_SUITE_P(
    FilAliases,
    LanguageSelectorAliasTest,
    ::testing::Combine(::testing::Values(STRING16_LITERAL("fil")),
                       ::testing::Values(STRING16_LITERAL("tl"))));

INSTANTIATE_TEST_SUITE_P(
    ZhCnAliases,
    LanguageSelectorAliasTest,
    ::testing::Combine(::testing::Values(STRING16_LITERAL("zh-cn")),
                       ::testing::Values(STRING16_LITERAL("zh-chs"),
                                         STRING16_LITERAL("zh-hans"),
                                         STRING16_LITERAL("zh-sg"))));

INSTANTIATE_TEST_SUITE_P(
    ZhTwAliases,
    LanguageSelectorAliasTest,
    ::testing::Combine(::testing::Values(STRING16_LITERAL("zh-tw")),
                       ::testing::Values(STRING16_LITERAL("zh-cht"),
                                         STRING16_LITERAL("zh-hant"),
                                         STRING16_LITERAL("zh-hk"),
                                         STRING16_LITERAL("zh-mo"))));

// Test that we can get a match of the default language.
TEST(LanguageSelectorTest, DefaultLanguageName) {
  TestLanguageSelector instance;
  EXPECT_FALSE(instance.selected_translation().empty());
}

// All languages given to the selector must be lower cased (since generally
// the language names are generated by a python script).
TEST(LanguageSelectorTest, InvalidLanguageCasing) {
  constexpr LanguageSelector::LangToOffset kLangToOffset[] = {
      {STRING16_LITERAL("en-US"), 0}};
  EXPECT_DCHECK_DEATH(LanguageSelector instance(
      std::vector<base::string16>({STRING16_LITERAL("en-us")}), kLangToOffset));
}

// Language name and offset pairs must be ordered when generated by the
// python script.
TEST(LanguageSelectorTest, InvalidLanguageNameOrder) {
  constexpr LanguageSelector::LangToOffset kLangToOffset[] = {
      {STRING16_LITERAL("en-us"), 0}, {STRING16_LITERAL("en-gb"), 1}};
  EXPECT_DCHECK_DEATH(LanguageSelector instance(
      std::vector<base::string16>({STRING16_LITERAL("en-us")}), kLangToOffset));
}

// There needs to be a fallback language available in the generated
// languages if ever the selector is given a language that does not exist.
TEST(LanguageSelectorTest, NoFallbackLanguageAvailable) {
  constexpr LanguageSelector::LangToOffset kLangToOffset[] = {
      {STRING16_LITERAL("en-gb"), 0}};
  EXPECT_DCHECK_DEATH(LanguageSelector instance(
      std::vector<base::string16>({STRING16_LITERAL("aa-bb")}), kLangToOffset));
}

// No languages available.
TEST(LanguageSelectorTest, NoLanguagesAvailable) {
  EXPECT_DCHECK_DEATH(LanguageSelector instance(
      std::vector<base::string16>({STRING16_LITERAL("en-us")}), {}));
}

}  // namespace i18n
}  // namespace win
}  // namespace base
